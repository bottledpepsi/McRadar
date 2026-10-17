#include "Wire.h"
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
    if (line.rfind("MCRADAR 1", 0) != 0 && line.rfind("MCRADAR 2", 0) != 0) return false; // not ours

    WorldSnapshot snap;
    snap.status = "agent connected";
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 2 || line[1] != ' ') continue;
        const char tag = line[0];
        const std::string rest = line.substr(2);

        if (tag == 'S')
        {
            snap.status = rest;
            continue;
        }

        std::istringstream ls(rest);
        if (tag == 'L' || tag == 'P')
        {
            std::string name, sx, sy, sz, syaw = "-", spitch = "-", shealth = "-";
            if (!(ls >> name >> sx >> sy >> sz)) continue;
            ls >> syaw >> spitch >> shealth; // optional columns (v1 has only yaw)
            PlayerState p;
            p.name = name;
            float x, y, z;
            if (!Num(sx, x) || !Num(sy, y) || !Num(sz, z)) continue;
            p.pos = {x, y, z};
            p.yawValid = Num(syaw, p.yawDeg);
            p.pitchValid = Num(spitch, p.pitchDeg);
            p.healthValid = Num(shealth, p.health);
            if (tag == 'L')
            {
                snap.local = std::move(p);
                snap.localValid = true;
            }
            else snap.players.push_back(std::move(p));
        }
        else if (tag == 'I')
        {
            std::string name, scount, sx, sy, sz;
            if (!(ls >> name >> scount >> sx >> sy >> sz)) continue;
            float x, y, z, c = 1;
            if (!Num(sx, x) || !Num(sy, y) || !Num(sz, z)) continue;
            Num(scount, c);
            ItemState it;
            for (auto& ch : name) if (ch == '_') ch = ' ';
            it.name = name;
            it.count = static_cast<int>(c);
            it.pos = {x, y, z};
            snap.items.push_back(std::move(it));
        }
    }
    out = std::move(snap);
    return true;
}
