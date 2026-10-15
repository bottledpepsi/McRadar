#include "Config.h"
#include "Logger.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace {

std::string Trim(std::string s) {
    auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

// Thin wrapper over GetPrivateProfile*A. NOTE: Win32 only looks in %WINDIR% for relative
// paths, so the caller must pass a full path (done in Config::Load).
class IniFile {
public:
    explicit IniFile(std::string fullPath) : path_(std::move(fullPath)) {}

    std::string Str(const char* sec, const char* key, const char* def = "") const {
        char buf[1024] = {};
        DWORD n = GetPrivateProfileStringA(sec, key, def, buf, sizeof buf, path_.c_str());
        return Trim(std::string(buf, n));
    }
    uint64_t UInt(const char* sec, const char* key, uint64_t def) const {
        std::string s = Str(sec, key);
        return s.empty() ? def : std::strtoull(s.c_str(), nullptr, 0);  // base 0 => "0x.." ok
    }
    double Dbl(const char* sec, const char* key, double def) const {
        std::string s = Str(sec, key);
        return s.empty() ? def : std::strtod(s.c_str(), nullptr);
    }
    bool Bool(const char* sec, const char* key, bool def) const {
        std::string s = Str(sec, key);
        if (s.empty()) return def;
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s == "1" || s == "true" || s == "yes" || s == "on";
    }
    std::vector<uintptr_t> List(const char* sec, const char* key) const {
        std::vector<uintptr_t> out;
        std::stringstream ss(Str(sec, key));
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            tok = Trim(tok);
            if (!tok.empty()) out.push_back(static_cast<uintptr_t>(std::strtoull(tok.c_str(), nullptr, 0)));
        }
        return out;
    }
    PointerChain Chain(const char* sec) const {
        PointerChain c;
        c.module = Str(sec, "module");
        c.base = static_cast<uintptr_t>(UInt(sec, "base", 0));
        c.offsets = List(sec, "offsets");
        return c;
    }

private:
    std::string path_;
};

}  // namespace

Config Config::Load(const std::string& path) {
    Config cfg;

    char full[MAX_PATH] = {};
    DWORD n = GetFullPathNameA(path.c_str(), MAX_PATH, full, nullptr);
    if (n == 0 || n >= MAX_PATH || GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES) {
        LOG_WARN("Config file '%s' not found - using built-in defaults (no offsets configured).",
                 path.c_str());
        return cfg;
    }
    LOG_INFO("Loading config: %s", full);
    IniFile ini(full);

    cfg.pollHz           = static_cast<int>(ini.UInt("general", "poll_hz", cfg.pollHz));
    cfg.radarRange       = ini.Dbl("general", "radar_range", cfg.radarRange);
    cfg.rotateWithPlayer = ini.Bool("general", "rotate_with_player", cfg.rotateWithPlayer);
    cfg.clampToEdge      = ini.Bool("general", "clamp_to_edge", cfg.clampToEdge);
    {
        std::string src = ini.Str("general", "source", "agent");
        std::transform(src.begin(), src.end(), src.begin(), [](unsigned char c) { return std::tolower(c); });
        if (src == "memory") cfg.source = SourceMode::Memory;
        else if (src == "demo") cfg.source = SourceMode::Demo;
        else cfg.source = SourceMode::Agent;
        if (ini.Bool("general", "demo", false)) cfg.source = SourceMode::Demo;   // legacy key
    }
    cfg.forcePid         = static_cast<uint32_t>(ini.UInt("general", "force_pid", 0));
    cfg.agent.javaPath   = ini.Str("agent", "java_path", "");
    cfg.agent.jarPath    = ini.Str("agent", "jar", cfg.agent.jarPath.c_str());
    cfg.agent.port       = static_cast<int>(ini.UInt("agent", "port", cfg.agent.port));
    cfg.agent.autoInject = ini.Bool("agent", "auto_inject", cfg.agent.autoInject);

    cfg.compressedOops = ini.Bool("jvm", "compressed_oops", cfg.compressedOops);
    cfg.heapBase       = static_cast<uintptr_t>(ini.UInt("jvm", "heap_base", cfg.heapBase));
    cfg.oopShift       = static_cast<unsigned>(ini.UInt("jvm", "oop_shift", cfg.oopShift));

    cfg.layout.posX = static_cast<uintptr_t>(ini.UInt("object", "pos_x", cfg.layout.posX));
    cfg.layout.posY = static_cast<uintptr_t>(ini.UInt("object", "pos_y", cfg.layout.posY));
    cfg.layout.posZ = static_cast<uintptr_t>(ini.UInt("object", "pos_z", cfg.layout.posZ));
    cfg.layout.yaw  = static_cast<uintptr_t>(ini.UInt("object", "yaw", cfg.layout.yaw));

    cfg.localChain = ini.Chain("local");

    cfg.playersEnabled = ini.Bool("player_list", "enabled", cfg.playersEnabled);
    cfg.listChain       = ini.Chain("player_list");
    cfg.listSizeOffset  = static_cast<uintptr_t>(ini.UInt("player_list", "size_offset", cfg.listSizeOffset));
    cfg.listArrayOffset = static_cast<uintptr_t>(ini.UInt("player_list", "array_offset", cfg.listArrayOffset));
    cfg.arrayDataOffset = static_cast<uintptr_t>(ini.UInt("player_list", "array_data_offset", cfg.arrayDataOffset));
    cfg.maxEntities     = static_cast<size_t>(ini.UInt("player_list", "max_entities", cfg.maxEntities));
    return cfg;
}

bool Config::Validate() const {
    bool ok = true;
    if (agent.port < 1024 || agent.port > 65535) {
        LOG_ERROR("[agent] port=%d is out of range (1024..65535).", agent.port);
        ok = false;
    }
    if (source != SourceMode::Memory) return ok;   // the checks below only matter for memory mode
    if (layout.Span() > 4096) {
        LOG_ERROR("[object] offsets span %llu bytes; keep pos_x/pos_y/pos_z/yaw within 4 KB of each other.",
                  static_cast<unsigned long long>(layout.Span()));
        ok = false;
    }
    if (compressedOops && oopShift > 7) {
        LOG_ERROR("[jvm] oop_shift=%u is not plausible (expected 0..7, usually 3).", oopShift);
        ok = false;
    }
    if (playersEnabled && compressedOops && heapBase == 0) {
        LOG_ERROR("[player_list] enabled with compressed oops but [jvm] heap_base is 0.");
        ok = false;
    }
    if (playersEnabled && !listChain.Configured()) {
        LOG_ERROR("[player_list] enabled but module/base are not configured.");
        ok = false;
    }
    return ok;
}
