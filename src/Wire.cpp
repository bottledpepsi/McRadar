#include "Wire.h"
#include "ItemText.h"
#include <cstdlib>
#include <sstream>

namespace
{
    // "-" -> unknown. strtod is locale-independent here because the app never calls setlocale().
    bool Num(const std::string& s, float& out)
    {
        if (s == "-" || s.empty()) return false;
        char* end = nullptr;
        out = static_cast<float>(std::strtod(s.c_str(), &end));
        return end && *end == 0;
    }
} // namespace

bool ParseDatagram(const char* data, size_t len, WorldSnapshot& out)
{
    std::istringstream in(std::string(data, len));
    std::string line;
    if (!std::getline(in, line)) return false;
    int version = 0;
    if (line.rfind("MCRADAR 1", 0) == 0) version = 1;
    else if (line.rfind("MCRADAR 2", 0) == 0) version = 2;
    else if (line.rfind("MCRADAR 3", 0) == 0) version = 3;
    else return false; // not ours

    WorldSnapshot snap;
    snap.status = "agent connected";
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 2 || line[1] != ' ') continue;
        const char tag = line[0];
        std::istringstream ls(line.substr(2));

        if (tag == 'S')
        {
            snap.status = line.substr(2);
            continue;
        }

        if (tag == 'L' || tag == 'P')
        {
            std::string name, sx, sy, sz, syaw = "-", spitch = "-", shealth = "-", sw = "-", sh = "-", uuid = "-";
            if (!(ls >> name >> sx >> sy >> sz)) continue;
            ls >> syaw >> spitch >> shealth; // v1: yaw only, v2: + pitch/health
            if (version >= 3) ls >> sw >> sh >> uuid; // v3: + hitbox size, uuid
            PlayerState p;
            p.name = name;
            float x, y, z;
            if (!Num(sx, x) || !Num(sy, y) || !Num(sz, z)) continue;
            p.pos = {x, y, z};
            p.yawValid = Num(syaw, p.yawDeg);
            p.pitchValid = Num(spitch, p.pitchDeg);
            p.healthValid = Num(shealth, p.health);
            float w = 0, h = 0;
            p.sizeValid = Num(sw, w) && Num(sh, h);
            if (p.sizeValid)
            {
                p.width = w;
                p.height = h;
            }
            p.uuid = uuid == "-" ? "" : uuid;
            if (tag == 'L')
            {
                snap.local = std::move(p);
                snap.localValid = true;
            }
            else snap.players.push_back(std::move(p));
        }
        else if (tag == 'C' && version >= 3)
        {
            std::string sx, sy, sz, syaw, spitch, sfov, smode;
            if (!(ls >> sx >> sy >> sz >> syaw >> spitch >> sfov >> smode)) continue;
            float x, y, z, yaw, pitch;
            if (!Num(sx, x) || !Num(sy, y) || !Num(sz, z) || !Num(syaw, yaw) || !Num(spitch, pitch)) continue;
            CameraState& c = snap.camera;
            c.valid = true;
            c.pos = {x, y, z};
            c.yawDeg = yaw;
            c.pitchDeg = pitch;
            c.fovValid = Num(sfov, c.fovDeg);
            c.mode = std::atoi(smode.c_str());
            if (c.mode < 1 || c.mode > 3) c.mode = 1;
        }
        else if (tag == 'I' && version >= 3)
        {
            std::string sx, sy, sz, sw, sh, scount, key, ench, tags;
            if (!(ls >> sx >> sy >> sz >> sw >> sh >> scount >> key >> ench >> tags)) continue;
            float x, y, z, w = 0, h = 0, cnt = 1;
            if (!Num(sx, x) || !Num(sy, y) || !Num(sz, z)) continue;
            Num(scount, cnt);
            ItemState it;
            it.pos = {x, y, z};
            it.sizeValid = Num(sw, w) && Num(sh, h);
            if (it.sizeValid)
            {
                it.width = w;
                it.height = h;
            }
            it.count = static_cast<int>(cnt);
            it.key = PercentDecode(key);
            it.enchants = ParseEnchants(PercentDecode(ench));
            it.tags = PercentDecode(tags);
            snap.items.push_back(std::move(it));
        }
    }
    out = std::move(snap);
    return true;
}
