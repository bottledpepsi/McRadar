// ScreenBox.h - screen-space rectangle of a world-space box, shared by the renderer and the unit tests.
#pragma once
#include "Camera.h"
#include <algorithm>

struct ScreenBox
{
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// Projects the box standing on `pos` (feet centre) with horizontal size `w` and height `h`.
// Returns false when the box centre is behind the camera or the whole box is off screen.
//
// Boxes smaller than `minPx` are grown around their centre instead of being dropped. A 0.25-block item is only
// ~2 px at 100 blocks (1080p, 70 deg FOV) - and perspective makes it larger toward the screen edge - so a
// "too small" cull made distant items vanish when looked at directly and reappear at the edge of the view.
inline bool ProjectBox(const Camera& cam, const Vec3& pos, double w, double h, float minPx, ScreenBox& out)
{
    float cx, cy;
    if (!cam.Project({pos.x, pos.y + h * 0.5, pos.z}, cx, cy)) return false;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (int xi = 0; xi < 2; ++xi)
        for (int yi = 0; yi < 2; ++yi)
            for (int zi = 0; zi < 2; ++zi)
            {
                float sx, sy;
                cam.ProjectClamped({
                                       pos.x + (xi ? w * 0.5 : -w * 0.5), pos.y + (yi ? h : 0.0),
                                       pos.z + (zi ? w * 0.5 : -w * 0.5)
                                   }, sx, sy);
                minX = std::min(minX, sx);
                maxX = std::max(maxX, sx);
                minY = std::min(minY, sy);
                maxY = std::max(maxY, sy);
            }
    if (maxX - minX < minPx)
    {
        const float mid = (minX + maxX) * 0.5f;
        minX = mid - minPx * 0.5f;
        maxX = mid + minPx * 0.5f;
    }
    if (maxY - minY < minPx)
    {
        const float mid = (minY + maxY) * 0.5f;
        minY = mid - minPx * 0.5f;
        maxY = mid + minPx * 0.5f;
    }
    if (maxX < 0 || minX > cam.w || maxY < 0 || minY > cam.h) return false;
    out = {minX, minY, maxX, maxY};
    return true;
}
