// Smoother.h - the agent sends 20-60 snapshots/s but the game renders at 144+ FPS. Boxes would
// stutter and, worse, the reconstructed camera would lag behind head turns. Each frame we
// extrapolate every player (and the local camera) from its last two samples:
//     value(now) = last + (last - previous) / dtSamples * min(now - tLast, 100 ms)
// Yaw is unwrapped so the 359 -> 0 wrap does not spin the box. Purely cosmetic; can be turned off.
#pragma once
#include "Types.h"
#include <chrono>
#include <cmath>
#include <unordered_map>

class Smoother
{
public:
    using Clock = std::chrono::steady_clock;

    // Call whenever a snapshot with a new seq arrives.
    void Push(const WorldSnapshot& s)
    {
        prev_ = cur_;
        cur_ = s;
        havePrev_ = haveCur_;
        haveCur_ = true;
        prevByName_.clear();
        for (const auto& p : prev_.players) prevByName_[p.name] = &p;
    }

    bool HasData() const { return haveCur_; }

    WorldSnapshot Predict(Clock::time_point now, bool enable)
    {
        WorldSnapshot out;
        PredictInto(now, enable, out);
        return out;
    }

    // Same as Predict(), but writes into a caller-owned snapshot. Copy-assignment reuses the vectors' and strings'
    // existing capacity, so in steady state this allocates nothing (Predict() deep-copies every item's strings
    // - name, enchants, NBT text - on every rendered frame).
    void PredictInto(Clock::time_point now, bool enable, WorldSnapshot& out)
    {
        out = cur_;
        if (!enable || !havePrev_) return;

        const double dtSamples = std::chrono::duration<double>(cur_.arrived - prev_.arrived).count();
        if (dtSamples < 0.004 || dtSamples > 0.5) return; // stale / bursty -> do not extrapolate
        const double ahead = std::fmin(0.10, std::fmax(0.0, std::chrono::duration<double>(now - cur_.arrived).count()));
        const double f = ahead / dtSamples;

        auto extrap = [&](PlayerState& c, const PlayerState& p)
        {
            c.pos.x += (c.pos.x - p.pos.x) * f;
            c.pos.y += (c.pos.y - p.pos.y) * f;
            c.pos.z += (c.pos.z - p.pos.z) * f;
            if (c.yawValid && p.yawValid) c.yawDeg += static_cast<float>(Wrap(c.yawDeg - p.yawDeg) * f);
            if (c.pitchValid && p.pitchValid) c.pitchDeg += static_cast<float>((c.pitchDeg - p.pitchDeg) * f);
        };
        if (out.localValid && prev_.localValid) extrap(out.local, prev_.local);
        if (out.camera.valid && prev_.camera.valid && out.camera.mode == prev_.camera.mode)
        {
            CameraState& c = out.camera;
            const CameraState& p = prev_.camera;
            c.pos.x += (c.pos.x - p.pos.x) * f;
            c.pos.y += (c.pos.y - p.pos.y) * f;
            c.pos.z += (c.pos.z - p.pos.z) * f;
            c.yawDeg += static_cast<float>(Wrap(c.yawDeg - p.yawDeg) * f);
            c.pitchDeg += static_cast<float>((c.pitchDeg - p.pitchDeg) * f);
            if (c.fovValid && p.fovValid) c.fovDeg += static_cast<float>((c.fovDeg - p.fovDeg) * f);
        }
        for (auto& pl : out.players)
        {
            auto it = prevByName_.find(pl.name);
            if (it != prevByName_.end()) extrap(pl, *it->second);
        }
    }

private:
    static double Wrap(double d)
    {
        while (d > 180) d -= 360;
        while (d < -180) d += 360;
        return d;
    } // shortest angular delta

    WorldSnapshot prev_, cur_;
    bool havePrev_ = false, haveCur_ = false;
    std::unordered_map<std::string, const PlayerState*> prevByName_;
};
