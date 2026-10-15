// GameReader.h - turns raw process memory into a WorldSnapshot using the Config.
// This is the ONLY place that knows about Minecraft's in-memory layout; swap/adjust it
// (or just edit radar.ini) when a new Minecraft/JVM version changes the offsets.
#pragma once
#include "Config.h"
#include "MemoryReader.h"
#include "ProcessManager.h"
#include "Types.h"
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct ReadResult {
    bool ok = false;        // false => local player could not be read this tick
    std::string error;      // human-readable reason (used for status + de-duplicated logging)
    WorldSnapshot snap;
};

class GameReader {
public:
    GameReader(const ProcessManager& pm, const MemoryReader& mem, const Config& cfg)
        : pm_(pm), mem_(mem), cfg_(cfg) {}

    ReadResult Read();

private:
    std::optional<uintptr_t> ResolveChain(const PointerChain& chain, std::string& err);
    std::optional<uintptr_t> ModuleBase(const std::string& name);
    // Decodes a JVM object reference (compressed 32-bit or raw 64-bit). 0 == null.
    uintptr_t DecodeRef(uint64_t raw) const;
    std::optional<PlayerState> ReadPlayer(uintptr_t object);
    bool ReadPlayers(uintptr_t localObject, WorldSnapshot& snap, std::string& err);

    const ProcessManager& pm_;
    const MemoryReader& mem_;
    const Config& cfg_;
    std::unordered_map<std::string, uintptr_t> moduleCache_;
    std::chrono::steady_clock::time_point lastModuleLookup_{};
    std::vector<unsigned char> scratch_;   // reused buffer: avoids per-tick allocations
    std::vector<uint32_t> refs32_;
    std::vector<uint64_t> refs64_;
};

// Synthetic data source so the radar can be exercised with no game running (--demo).
class DemoSource {
public:
    DemoSource() : start_(std::chrono::steady_clock::now()) {}
    WorldSnapshot Tick() const;
private:
    std::chrono::steady_clock::time_point start_;
};
