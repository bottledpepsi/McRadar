// MC Radar - educational player radar for a LOCAL Minecraft Java world (e.g. LAN with 2 accounts).
//
// Data sources (radar.ini [general] source=, or flags):
//   agent  (default) a small Java agent is loaded into the game's JVM through the official Attach
//                    API; it finds the players by structure (version independent) and streams
//                    them over loopback UDP. It also exports the offsets it discovered.
//   memory           process discovery -> ReadProcessMemory with offsets you configure by hand
//   demo             synthetic players, no process access
//
// Usage:
//   mcradar [--config radar.ini] [--agent | --memory | --demo] [--pid N]
//   mcradar --scan X Y Z [tolerance]     find addresses holding your F3 coordinates
//   mcradar --watch 0xADDRESS            live-print the position stored at an address
//
// Thread model:
//   main/UI thread : Win32 message loop + GDI painting (~60 FPS, timer driven)
//   Poller thread  : attach / read memory at ~25 Hz, publishes WorldSnapshot via SharedState
#include "Config.h"
#include "GameReader.h"
#include "Logger.h"
#include "MemoryReader.h"
#include "MemoryScanner.h"
#include "Platform.h"
#include "Poller.h"
#include "ProcessManager.h"
#include "RadarWindow.h"
#include "Types.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

std::atomic_bool g_stop{false};
BOOL WINAPI ConsoleHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_stop = true;
        return TRUE;
    }
    return FALSE;
}

void PrintUsage() {
    std::puts(
        "MC Radar (educational, single-player testing)\n"
        "  mcradar [--config radar.ini] [--agent | --memory | --demo] [--pid N]\n"
        "  mcradar [--config radar.ini] --scan X Y Z [tolerance]\n"
        "  mcradar [--config radar.ini] --watch 0xADDRESS\n"
        "\nKeys in the radar window: wheel/+/- zoom, R rotate, C clamp, T topmost, Esc quit.");
}

int RunScan(double x, double y, double z, double tol, const Config& cfg) {
    ProcessManager pm;
    pm.SetForcedPid(cfg.forcePid);
    if (!pm.Attach()) { LOG_ERROR("Minecraft is not running (or could not be opened)."); return 1; }
    LOG_INFO("Scanning private memory for doubles near (%.3f, %.3f, %.3f), tol %.4f ... (can take a few seconds)",
             x, y, z, tol);
    auto hits = ScanForDoubleTriples(pm.Handle(), x, y, z, tol);
    for (uintptr_t a : hits) std::printf("  0x%llX\n", static_cast<unsigned long long>(a));
    LOG_INFO("%llu candidate(s). Move in-game and use --watch <addr> to see which one updates live.",
             static_cast<unsigned long long>(hits.size()));
    return hits.empty() ? 2 : 0;
}

int RunWatch(uintptr_t addr, const Config& cfg) {
    ProcessManager pm;
    pm.SetForcedPid(cfg.forcePid);
    if (!pm.Attach()) { LOG_ERROR("Minecraft is not running (or could not be opened)."); return 1; }
    MemoryReader mem(pm.Handle());
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    LOG_INFO("Watching 0x%llX (Ctrl+C to stop)", static_cast<unsigned long long>(addr));
    while (!g_stop && pm.IsAlive()) {
        auto x = mem.Read<double>(addr + cfg.layout.posX);
        auto y = mem.Read<double>(addr + cfg.layout.posY);
        auto z = mem.Read<double>(addr + cfg.layout.posZ);
        auto yaw = mem.Read<float>(addr + cfg.layout.yaw);
        if (x && y && z && yaw)
            std::printf("\rx=%10.3f y=%8.3f z=%10.3f yaw=%8.2f   ", *x, *y, *z, *yaw);
        else
            std::printf("\r(unreadable - error %lu)                                ",
                        static_cast<unsigned long>(mem.LastError()));
        std::fflush(stdout);
        Sleep(100);
    }
    std::puts("");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string configPath = "radar.ini";
    bool scan = false, watch = false, srcOverride = false;
    SourceMode srcMode = SourceMode::Agent;
    uint32_t pidOverride = 0;
    double sx = 0, sy = 0, sz = 0, tol = 0.01;
    uintptr_t watchAddr = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") { PrintUsage(); return 0; }
        else if (a == "--demo")   { srcMode = SourceMode::Demo;   srcOverride = true; }
        else if (a == "--memory") { srcMode = SourceMode::Memory; srcOverride = true; }
        else if (a == "--agent")  { srcMode = SourceMode::Agent;  srcOverride = true; }
        else if (a == "--config" && i + 1 < argc) configPath = argv[++i];
        else if (a == "--pid" && i + 1 < argc) pidOverride = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--scan" && i + 3 < argc) {
            scan = true;
            sx = std::strtod(argv[++i], nullptr);
            sy = std::strtod(argv[++i], nullptr);
            sz = std::strtod(argv[++i], nullptr);
            if (i + 1 < argc && argv[i + 1][0] != '-') tol = std::strtod(argv[++i], nullptr);
        } else if (a == "--watch" && i + 1 < argc) {
            watch = true;
            watchAddr = static_cast<uintptr_t>(std::strtoull(argv[++i], nullptr, 0));
        } else { PrintUsage(); return 1; }
    }

    Config cfg = Config::Load(configPath);
    if (srcOverride) cfg.source = srcMode;
    if (pidOverride) cfg.forcePid = pidOverride;
    if (!cfg.Validate()) return 1;

    if (scan) return RunScan(sx, sy, sz, tol, cfg);
    if (watch) return RunWatch(watchAddr, cfg);

    if (cfg.source == SourceMode::Memory && !cfg.localChain.Configured())
        LOG_WARN("[local] pointer chain is not configured in radar.ini - the radar will attach but show no data. "
                 "Run with --demo to test the UI, or see README.md to find your offsets.");

    SharedState state;
    Poller poller(cfg, state);
    poller.Start();                       // background thread begins polling immediately

    RadarWindow window(GetModuleHandleW(nullptr), state, cfg);
    if (!window.Create()) return 1;
    const int rc = window.Run();          // blocks until the window is closed

    poller.Stop();                        // join the worker before state/cfg go out of scope
    return rc;
}
