// Types.h - plain data shared between the memory-polling thread and the UI thread.
#pragma once
#include <mutex>
#include <string>
#include <utility>
#include <vector>

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

// One PLAYER (the local player or a remote player). The radar only ever draws players.
// Minecraft conventions: +X = east, +Z = south, +Y = up.
// yaw (degrees): 0 = facing +Z (south), 90 = facing -X (west), 180 = north, -90 = east.
struct PlayerState {
    Vec3 pos;
    float yawDeg = 0.0f;
    bool yawValid = true;      // false when the heading could not be determined
    std::string name;          // account name when known (agent mode), else empty
};

// Everything the renderer needs for one frame. Produced by the worker thread.
struct WorldSnapshot {
    bool attached = false;     // connected to a Minecraft process (or demo mode)
    bool localValid = false;   // local player data was read successfully this tick
    PlayerState local;
    std::vector<PlayerState> others;
    std::string status = "Starting...";
};

// Mutex-protected hand-off point between the poller (writer) and the renderer (reader).
// The snapshot is small (a few KB at most), so copying it out is cheaper and simpler
// than any lock-free scheme and guarantees the renderer never sees a half-written frame.
class SharedState {
public:
    void Publish(WorldSnapshot s) {
        std::lock_guard<std::mutex> lk(m_);
        snap_ = std::move(s);
    }
    WorldSnapshot Get() const {
        std::lock_guard<std::mutex> lk(m_);
        return snap_;
    }
private:
    mutable std::mutex m_;
    WorldSnapshot snap_;
};
