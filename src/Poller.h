// Poller.h - background worker thread producing WorldSnapshots for the UI.
//   Agent  : receive snapshots from the Java agent (and inject it when needed)
//   Memory : attach -> ReadProcessMemory with configured offsets (~pollHz)
//   Demo   : synthetic players
#pragma once
#include "Config.h"
#include "Types.h"
#include <stop_token>
#include <thread>

class Poller {
public:
    Poller(const Config& cfg, SharedState& state) : cfg_(cfg), state_(state) {}
    ~Poller() { Stop(); }
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;

    void Start();
    void Stop();   // requests stop and joins; safe to call repeatedly

private:
    void Run(std::stop_token st);
    void RunDemo(std::stop_token st);
    void RunMemory(std::stop_token st);
    void RunAgent(std::stop_token st);

    const Config& cfg_;
    SharedState& state_;
    std::jthread thread_;
};
