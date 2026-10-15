#include "Poller.h"
#include "AgentLauncher.h"
#include "AgentReceiver.h"
#include "GameReader.h"
#include "Logger.h"
#include "MemoryReader.h"
#include "ProcessManager.h"
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

void Poller::Start() {
    if (thread_.joinable()) return;
    thread_ = std::jthread([this](std::stop_token st) { Run(st); });
}

void Poller::Stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

void Poller::RunMemory(std::stop_token st) {
    const int hz = std::clamp(cfg_.pollHz, 5, 120);
    const auto period = std::chrono::microseconds(1'000'000 / hz);
    LOG_INFO("Memory source: polling at %d Hz.", hz);

    ProcessManager pm;
    pm.SetForcedPid(cfg_.forcePid);
    std::unique_ptr<MemoryReader> mem;
    std::unique_ptr<GameReader> game;

    Clock::time_point lastAttachTry{};
    std::string lastError;   // log each distinct error once, not 25x per second
    int failStreak = 0;

    auto next = Clock::now();
    while (!st.stop_requested()) {
        next += period;
        WorldSnapshot snap;

        if (!pm.IsAttached()) {
            snap.status = "Waiting for Minecraft (javaw.exe)...";
            if (Clock::now() - lastAttachTry >= 1s) {       // don't rescan processes every tick
                lastAttachTry = Clock::now();
                if (pm.Attach()) {
                    mem = std::make_unique<MemoryReader>(pm.Handle());
                    game = std::make_unique<GameReader>(pm, *mem, cfg_);
                    failStreak = 0;
                    lastError.clear();
                }
            }
        } else if (!pm.IsAlive()) {
            LOG_WARN("Minecraft process exited.");
            game.reset();
            mem.reset();
            pm.Detach();
            snap.status = "Minecraft closed - waiting for it to start again...";
        } else {
            ReadResult rr = game->Read();
            if (rr.ok) {
                if (failStreak > 0) LOG_INFO("Reads recovered.");
                failStreak = 0;
                lastError.clear();
                snap = std::move(rr.snap);
            } else {
                ++failStreak;
                if (rr.error != lastError) {
                    lastError = rr.error;
                    LOG_WARN("%s (last Win32 error %lu)", rr.error.c_str(),
                             static_cast<unsigned long>(mem->LastError()));
                }
                snap.attached = true;
                snap.status = "Attached, but no data: " + rr.error;
            }
        }

        state_.Publish(std::move(snap));

        // Fixed-rate loop: sleep until the next deadline rather than a fixed delay, so the
        // rate doesn't drift with read time. If we fell badly behind, resync instead of bursting.
        const auto now = Clock::now();
        if (next < now - 5 * period) next = now;
        std::this_thread::sleep_until(next);
    }
}

void Poller::Run(std::stop_token st) {
    switch (cfg_.source) {
        case SourceMode::Demo:   RunDemo(st);   break;
        case SourceMode::Memory: RunMemory(st); break;
        case SourceMode::Agent:  RunAgent(st);  break;
    }
    LOG_INFO("Poller stopped.");
}

void Poller::RunDemo(std::stop_token st) {
    LOG_INFO("Demo source: synthetic players, no process access.");
    DemoSource demo;
    while (!st.stop_requested()) {
        state_.Publish(demo.Tick());
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / std::clamp(cfg_.pollHz, 5, 120)));
    }
}

// Agent mode. Flow per Minecraft process:
//   1. find the JVM (window title / javaw.exe)
//   2. listen ~2 s: the agent may already be there (-javaagent launch, or a previous injection)
//   3. otherwise attach the agent through the JVM Attach API (AgentLauncher::Inject)
//   4. the agent discovers the players itself and streams UDP snapshots; we just publish them
void Poller::RunAgent(std::stop_token st) {
    LOG_INFO("Agent source: waiting for Minecraft; data arrives on UDP port %d.", cfg_.agent.port);
    AgentReceiver rx(static_cast<uint16_t>(cfg_.agent.port));
    const std::wstring jar = AgentLauncher::ResolveJar(cfg_.agent.jarPath);

    ProcessManager pm;
    pm.SetForcedPid(cfg_.forcePid);
    Clock::time_point lastAttachTry{}, attachedAt{}, lastPacket{}, lastInject{};
    bool haveData = false, everInjected = false, javaWarned = false;
    DWORD injectedPid = 0;
    WorldSnapshot last;

    while (!st.stop_requested()) {
        const auto now = Clock::now();
        WorldSnapshot snap;

        if (!rx.Ok()) {
            snap.status = "Cannot listen on UDP port " + std::to_string(cfg_.agent.port) + " (see console)";
            state_.Publish(std::move(snap));
            std::this_thread::sleep_for(500ms);
            continue;
        }

        if (!pm.IsAttached()) {
            snap.status = "Waiting for Minecraft (javaw.exe)...";
            haveData = false;
            if (now - lastAttachTry >= 1s) {
                lastAttachTry = now;
                if (pm.Attach()) { attachedAt = now; injectedPid = 0; everInjected = false; }
            }
            state_.Publish(std::move(snap));
            std::this_thread::sleep_for(200ms);
            continue;
        }
        if (!pm.IsAlive()) {
            LOG_WARN("Minecraft process exited.");
            pm.Detach();
            haveData = false;
            snap.status = "Minecraft closed - waiting for it to start again...";
            state_.Publish(std::move(snap));
            continue;
        }

        // Block up to 100 ms for data: this also paces the loop at the agent's send rate.
        WorldSnapshot pkt;
        if (rx.Receive(pkt, 100)) {
            haveData = true;
            lastPacket = Clock::now();
            last = pkt;
            state_.Publish(std::move(pkt));
            continue;
        }

        const auto silence = Clock::now() - (haveData ? lastPacket : attachedAt);
        if (haveData && silence < 1500ms) { state_.Publish(last); continue; }   // short gap: keep last frame

        snap.attached = true;
        if (silence > 2s && cfg_.agent.autoInject &&
            (injectedPid != pm.Pid() || Clock::now() - lastInject > 30s)) {
            snap.status = "Attaching agent to Minecraft...";
            state_.Publish(snap);                                   // show progress; injection blocks
            lastInject = Clock::now();
            injectedPid = pm.Pid();
            if (auto java = AgentLauncher::FindJava(cfg_.agent.javaPath)) {
                everInjected = AgentLauncher::Inject(pm.Pid(), *java, jar, cfg_.agent.port, st) || everInjected;
            } else if (!javaWarned) {
                javaWarned = true;
                LOG_ERROR("No java.exe found. Install a JDK 17+ and set JAVA_HOME or [agent] java_path.");
            }
            continue;
        }
        snap.status = !cfg_.agent.autoInject ? "Waiting for the agent (auto_inject=0; start the game with -javaagent)"
                    : (everInjected ? "Agent attached, waiting for data..." : "Agent not loaded (see console for the reason)");
        if (haveData) snap.status = "Agent went silent (game closing / world unloading?)";
        state_.Publish(std::move(snap));
    }
}
