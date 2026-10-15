// Config.h - everything that is version-specific lives here (and in radar.ini).
//
// Minecraft Java runs inside the JVM, so there is no fixed "player struct" at a fixed
// address. Offsets depend on the Minecraft version, the mappings (vanilla / Fabric / Forge),
// the JVM build, the GC, and whether compressed oops are on. Instead of hard-coding guesses,
// every such value is a field below that can be overridden in radar.ini without recompiling.
#pragma once
#include "Platform.h"
#include <algorithm>
#include <string>
#include <vector>

// A pointer path:  address = [ [ [module+base] + o0 ] + o1 ] ... + oN
// Rules:
//   * module  : optional DLL name (e.g. "jvm.dll"). Empty => `base` is an absolute address.
//   * offsets : empty => result is simply module+base (NO dereference). Use this when you
//               already know the absolute address of the object (e.g. from --scan).
//               non-empty => for each offset: cur = *(uint64*)(cur + offset). The first step
//               reads at (module+base + o0), so use o0 = 0 to dereference the base itself.
//   * The result is the address of an object; field offsets (ObjectLayout) are added to it.
struct PointerChain {
    std::string module;
    uintptr_t base = 0;
    std::vector<uintptr_t> offsets;
    bool Configured() const { return !module.empty() || base != 0; }
};

// Field offsets inside an Entity object (same layout is assumed for local + remote entities).
// PLACEHOLDERS - replace via radar.ini. In many versions x/y/z are three consecutive doubles
// (a Vec3 'position' object or fields on Entity), and yaw is a float.
struct ObjectLayout {
    uintptr_t posX = 0x00;
    uintptr_t posY = 0x08;
    uintptr_t posZ = 0x10;
    uintptr_t yaw  = 0x18;

    uintptr_t MinOffset() const { return std::min({posX, posY, posZ, yaw}); }
    uintptr_t MaxEnd() const { return std::max({posX + 8, posY + 8, posZ + 8, yaw + 4}); }
    uintptr_t Span() const { return MaxEnd() - MinOffset(); }  // bytes fetched per entity read
};

// Where player data comes from.
enum class SourceMode {
    Agent,    // default: a Java agent loaded into the JVM discovers the players (version independent)
    Memory,   // ReadProcessMemory with hand-configured offsets / pointer chains (radar.ini)
    Demo      // synthetic data, no process access
};

// Settings for SourceMode::Agent.
struct AgentSettings {
    std::string javaPath;                  // java.exe of a full JDK; empty = JAVA_HOME, then PATH
    std::string jarPath = "mcradar-agent.jar";   // relative paths are resolved against the exe's folder
    int         port = 47615;              // UDP port on 127.0.0.1 the agent sends to
    bool        autoInject = true;         // attach the agent automatically when Minecraft is found
};

struct Config {
    // --- general -----------------------------------------------------------
    SourceMode source = SourceMode::Agent;
    AgentSettings agent;
    uint32_t forcePid = 0;             // attach to this PID instead of auto-detecting (0 = auto)
    int    pollHz = 25;                // worker thread polling rate (clamped to 5..120)
    double radarRange = 64.0;          // blocks from centre to the edge of the radar
    bool   rotateWithPlayer = true;    // heading-up (true) or north-up (false)
    bool   clampToEdge = false;        // draw out-of-range entities pinned to the rim

    // --- JVM object references --------------------------------------------
    // HotSpot stores object references as 32-bit "compressed oops" when the heap is < ~32 GB:
    //     address = heapBase + (narrow << shift)
    // Start the game with -XX:-UseCompressedOops to get plain 8-byte references instead.
    bool      compressedOops = true;
    uintptr_t heapBase = 0;            // base of the Java heap (see README)
    unsigned  oopShift = 3;            // usually 3 (8-byte object alignment)

    // --- local player ------------------------------------------------------
    PointerChain localChain;           // resolves to the LocalPlayer object
    ObjectLayout layout;

    // --- other PLAYERS (memory mode only) ----------------------------------
    // Point this at a list that contains ONLY players (e.g. the level's player list). The radar
    // draws everything in it, so a mixed "all entities" list would show mobs too.
    // Example: a java.util.ArrayList<Player>
    //   listObject  --(+listSizeOffset)-->  int size
    //   listObject  --(+listArrayOffset)--> reference to Object[]
    //   Object[]    --(+arrayDataOffset)--> first element reference (12 or 16 = header size)
    bool      playersEnabled = false;
    PointerChain listChain;
    uintptr_t listSizeOffset = 0x10;
    uintptr_t listArrayOffset = 0x14;
    uintptr_t arrayDataOffset = 0x10;
    size_t    maxEntities = 256;

    // Loads radar.ini (relative paths are resolved against the CWD). Missing file => defaults.
    static Config Load(const std::string& path);
    // Sanity checks; logs and returns false for unusable values.
    bool Validate() const;
};
