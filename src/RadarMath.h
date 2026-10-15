// RadarMath.h - world -> radar-pixel transformation.
#pragma once
#include "Types.h"
#include <cmath>

struct Vec2 { double x = 0.0, y = 0.0; };

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kDegToRad = kPi / 180.0;

// Returns the pixel offset (relative to the radar centre, +x right, +y DOWN) of `target`.
//
// Only X/Z matter for a top-down radar; Y (height) is ignored.
//
// Step 1 - translate: delta = target - origin, so the local player sits at (0,0).
//
// Step 2a - "north-up" mode (rotate == false): the map is fixed to the world.
//          World -Z (north) is screen-up, +X (east) is screen-right, so
//          screen = (dx, dz) * scale.
//
// Step 2b - "heading-up" mode (rotate == true): rotate the delta into the player's frame.
//          Minecraft's forward vector for yaw θ is  f = (-sin θ, cos θ)  in (x, z).
//          The player's right-hand vector is        r = (-cos θ, -sin θ).
//          (Check: θ = 0 faces +Z, so right-hand is -X = (-1, 0). OK.
//                  θ = 90 faces -X, so right-hand is -Z = (0, -1). OK.)
//          forward = delta·f,  right = delta·r
//          screen  = ( right * scale, -forward * scale )   // screen-up = forward
inline Vec2 WorldToRadarOffset(const Vec3& origin, double yawDeg, const Vec3& target,
                               double pxPerBlock, bool rotate) {
    const double dx = target.x - origin.x;
    const double dz = target.z - origin.z;
    if (!rotate) return {dx * pxPerBlock, dz * pxPerBlock};

    const double yaw = yawDeg * kDegToRad;
    const double s = std::sin(yaw), c = std::cos(yaw);
    const double forward = dx * (-s) + dz * c;
    const double right   = dx * (-c) + dz * (-s);
    return {right * pxPerBlock, -forward * pxPerBlock};
}
