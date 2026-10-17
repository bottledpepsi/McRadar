// Demo.h - synthetic world for --demo: lets you try the overlay, config UI, friends and projection
// without Minecraft. The "local player" turns slowly; other players walk around it.
#pragma once
#include "Types.h"
#include <chrono>
#include <cmath>

class DemoSource
{
public:
    DemoSource() : t0_(std::chrono::steady_clock::now())
    {
    }

    WorldSnapshot Tick() const
    {
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
        WorldSnapshot s;
        s.status = "DEMO MODE - synthetic players";
        s.localValid = true;
        s.local.name = "You";
        s.local.pos = {0, 64, 0};
        s.local.yawDeg = static_cast<float>(std::fmod(t * 18.0, 360.0) - 180.0);
        s.local.pitchDeg = static_cast<float>(8.0 * std::sin(t * 0.4));
        s.local.yawValid = s.local.pitchValid = true;
        static const char* const names[] = {"Bob", "Carol", "Dave", "Erin", "Frank", "Grace"};
        for (int i = 0; i < 6; ++i)
        {
            const double r = 6.0 + 11.0 * i, a = t * (0.15 + 0.05 * i) + i * 1.3;
            PlayerState p;
            p.name = names[i];
            p.pos = {r * std::cos(a), 64.0 + (i % 3) * 0.5, r * std::sin(a)};
            p.yawDeg = static_cast<float>(std::fmod(-a * 57.2958 - 90.0, 360.0));
            p.health = static_cast<float>(20.0 - i * 3.0);
            p.yawValid = p.healthValid = true;
            s.players.push_back(p);
        }
        return s;
    }

private:
    std::chrono::steady_clock::time_point t0_;
};
