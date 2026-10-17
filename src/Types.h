// Types.h - data shared between the UDP thread and the UI/render thread.
#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

struct Vec3
{
    double x = 0, y = 0, z = 0;
};

// Minecraft conventions: +X east, +Y up, +Z south. yaw 0 = facing +Z, increasing clockwise seen from
// above (so yaw 90 faces -X / west); pitch +90 = looking straight down.
struct PlayerState
{
    Vec3 pos; // feet position
    float yawDeg = 0, pitchDeg = 0, health = 0;
    bool yawValid = false, pitchValid = false, healthValid = false;
    std::string name;
};

// Dropped item entity. The protocol carries them ("I" lines) but the current agent does not
// discover item entities yet, so this list stays empty unless an agent version sends them.
struct ItemState
{
    Vec3 pos;
    std::string name;
    int count = 1;
};

struct WorldSnapshot
{
    bool localValid = false;
    PlayerState local;
    std::vector<PlayerState> players; // OTHER players only
    std::vector<ItemState> items;
    std::string status; // agent status line
    uint64_t seq = 0; // increments per received datagram
    std::chrono::steady_clock::time_point arrived{};
};
