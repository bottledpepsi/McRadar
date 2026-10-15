#include "GameReader.h"
#include "Logger.h"
#include "RadarMath.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace {
std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}
bool SaneCoord(double v) { return std::isfinite(v) && std::fabs(v) < 3.0e7; }  // world border is 29,999,984
}  // namespace

std::optional<uintptr_t> GameReader::ModuleBase(const std::string& name) {
    const std::string key = Lower(name);
    if (auto it = moduleCache_.find(key); it != moduleCache_.end()) return it->second;

    // Module snapshots are comparatively expensive - throttle failed lookups to 1/second.
    const auto now = std::chrono::steady_clock::now();
    if (now - lastModuleLookup_ < std::chrono::seconds(1)) return std::nullopt;
    lastModuleLookup_ = now;

    if (auto base = pm_.ModuleBase(name)) {
        LOG_INFO("Module %s base = 0x%llX", name.c_str(), static_cast<unsigned long long>(*base));
        moduleCache_[key] = *base;
        return base;
    }
    return std::nullopt;
}

// address = [[[module+base] + o0] + o1] ...   (see PointerChain in Config.h)
std::optional<uintptr_t> GameReader::ResolveChain(const PointerChain& chain, std::string& err) {
    if (!chain.Configured()) { err = "pointer chain not configured"; return std::nullopt; }

    uintptr_t start = chain.base;
    if (!chain.module.empty()) {
        auto mod = ModuleBase(chain.module);
        if (!mod) { err = "module '" + chain.module + "' not found in target"; return std::nullopt; }
        start += *mod;
    }
    if (chain.offsets.empty()) return start;  // absolute / module-relative address, no deref

    auto addr = mem_.ResolveChain(start, chain.offsets);
    if (!addr) err = "pointer chain broken (stale pointer, wrong offsets, or world not loaded)";
    return addr;
}

uintptr_t GameReader::DecodeRef(uint64_t raw) const {
    if (raw == 0) return 0;
    if (!cfg_.compressedOops) return static_cast<uintptr_t>(raw);
    // HotSpot compressed oop: heapBase + (narrow << shift)
    return cfg_.heapBase + (static_cast<uintptr_t>(raw & 0xFFFFFFFFull) << cfg_.oopShift);
}

// Fetches [min offset .. max end) of the object in ONE ReadProcessMemory call and picks
// the fields out of the local copy - far fewer syscalls than 4 separate reads.
std::optional<PlayerState> GameReader::ReadPlayer(uintptr_t object) {
    const ObjectLayout& L = cfg_.layout;
    const uintptr_t lo = L.MinOffset();
    const size_t span = static_cast<size_t>(L.Span());
    scratch_.resize(span);
    if (!mem_.ReadRaw(object + lo, scratch_.data(), span)) return std::nullopt;

    auto get = [&](uintptr_t off, auto& out) {
        std::memcpy(&out, scratch_.data() + (off - lo), sizeof out);
    };
    double x, y, z;
    float yaw;
    get(L.posX, x); get(L.posY, y); get(L.posZ, z); get(L.yaw, yaw);

    // Garbage in, garbage out is the main failure mode with guessed offsets - reject it here.
    if (!SaneCoord(x) || !SaneCoord(y) || !SaneCoord(z) || !std::isfinite(yaw)) return std::nullopt;
    PlayerState ps;
    ps.pos = {x, y, z};
    ps.yawDeg = yaw;
    return ps;
}

bool GameReader::ReadPlayers(uintptr_t localObject, WorldSnapshot& snap, std::string& err) {
    std::string chainErr;
    auto listObj = ResolveChain(cfg_.listChain, chainErr);
    if (!listObj) { err = "player list: " + chainErr; return false; }

    auto size = mem_.Read<int32_t>(*listObj + cfg_.listSizeOffset);
    if (!size || *size < 0 || static_cast<size_t>(*size) > cfg_.maxEntities) {
        err = "player list: size unreadable or out of range";
        return false;
    }
    if (*size == 0) return true;
    const size_t count = static_cast<size_t>(*size);

    // listObject -> Object[] reference
    const size_t refSize = cfg_.compressedOops ? 4 : 8;
    uint64_t rawArr = 0;
    if (!mem_.ReadRaw(*listObj + cfg_.listArrayOffset, &rawArr, refSize)) {
        err = "player list: array reference unreadable";
        return false;
    }
    const uintptr_t array = DecodeRef(rawArr);
    if (!MemoryReader::IsPlausiblePointer(array)) { err = "player list: array pointer invalid"; return false; }

    // Read all element references in one go.
    const uintptr_t first = array + cfg_.arrayDataOffset;
    std::vector<uintptr_t> objects;
    objects.reserve(count);
    if (cfg_.compressedOops) {
        refs32_.resize(count);
        if (!mem_.ReadRaw(first, refs32_.data(), count * 4)) { err = "player list: element read failed"; return false; }
        for (uint32_t r : refs32_) objects.push_back(DecodeRef(r));
    } else {
        refs64_.resize(count);
        if (!mem_.ReadRaw(first, refs64_.data(), count * 8)) { err = "player list: element read failed"; return false; }
        for (uint64_t r : refs64_) objects.push_back(DecodeRef(r));
    }

    for (uintptr_t obj : objects) {
        if (!MemoryReader::IsPlausiblePointer(obj) || obj == localObject) continue;  // null / self
        if (auto e = ReadPlayer(obj)) snap.others.push_back(*e);   // silently skip dead/bad entries
    }
    return true;
}

ReadResult GameReader::Read() {
    ReadResult r;
    std::string err;

    auto localObj = ResolveChain(cfg_.localChain, err);
    if (!localObj) { r.error = "local player: " + err; return r; }

    auto local = ReadPlayer(*localObj);
    if (!local) { r.error = "local player: object unreadable or position out of range (check [object] offsets)"; return r; }

    r.snap.attached = true;
    r.snap.localValid = true;
    r.snap.local = *local;
    r.snap.status = "Connected (PID " + std::to_string(pm_.Pid()) + ")";

    if (cfg_.playersEnabled) {
        std::string entErr;
        if (!ReadPlayers(*localObj, r.snap, entErr)) {
            r.snap.others.clear();
            r.snap.status += " | " + entErr;   // non-fatal: the local dot still renders
        }
    }
    r.ok = true;
    return r;
}

WorldSnapshot DemoSource::Tick() const {
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    WorldSnapshot s;
    s.attached = s.localValid = true;
    s.status = "DEMO MODE (synthetic players)";
    static const char* const kNames[] = {"Bob", "Carol", "Dave", "Erin", "Frank", "Grace", "Heidi"};

    // Local player walks a slow circle while turning.
    s.local.pos = {100.0 + 20.0 * std::sin(t * 0.3), 64.0, -50.0 + 20.0 * std::cos(t * 0.3)};
    s.local.yawDeg = static_cast<float>(std::fmod(t * 25.0, 360.0) - 180.0);
    s.local.name = "Alice";

    // Several entities orbiting at different radii/speeds, plus one stationary.
    for (int i = 0; i < 6; ++i) {
        const double r = 10.0 + 9.0 * i;
        const double a = t * (0.4 + 0.15 * i) + i * 1.1;
        PlayerState p;
        p.pos = {s.local.pos.x + r * std::cos(a), 64.0, s.local.pos.z + r * std::sin(a)};
        p.name = kNames[i];
        s.others.push_back(p);
    }
    PlayerState idle;
    idle.pos = {s.local.pos.x + 25.0, 64.0, s.local.pos.z - 15.0};
    idle.name = kNames[6];
    s.others.push_back(idle);
    return s;
}
