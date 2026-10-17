// EspRenderer.h - draws the ESP (boxes / skeletons / tracers / labels / health / items) and the minimap.
// Port of the drawing half of EspOverlay.cs; the projection comes from Camera.h instead of GL matrices.
#pragma once
#include "Camera.h"
#include "EspConfig.h"
#include "Gfx.h"
#include "Types.h"

class EspRenderer
{
public:
    void DrawWorld(Gfx& g, const Settings& s, const Camera& cam, const WorldSnapshot& snap);
    void DrawMinimap(Gfx& g, const Settings& s, const Camera& cam, const WorldSnapshot& snap, int winW);

private:
    void DrawTracer(Gfx& g, const Settings& s, const Camera& cam, const Vec3& target, float dist, bool isFriend);
    void DrawPlayer(Gfx& g, const Settings& s, const Camera& cam, const PlayerState& p, float dist, bool isFriend);
    void DrawSkeleton(Gfx& g, const Settings& s, const Camera& cam, const PlayerState& p, float dist, bool isFriend);
    void DrawItem(Gfx& g, const Settings& s, const Camera& cam, const ItemState& it, float dist);
    void DrawLabels(Gfx& g, const Settings& s, const PlayerState& p, float dist, bool isFriend,
                    float cx, float top, float bottom);
};
