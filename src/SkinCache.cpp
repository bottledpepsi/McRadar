#include "SkinCache.h"
#include "Json.h"
#include "Logger.h"
#include "Platform.h"
#include "RaiiHandle.h"
#include <winhttp.h>
#include <wincodec.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace
{
    std::string Env(const char* name, const char* fallback)
    {
        char buf[512];
        DWORD n = GetEnvironmentVariableA(name, buf, sizeof buf);
        return (n > 0 && n < sizeof buf) ? std::string(buf, n) : std::string(fallback);
    }

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

    // ---- WinHTTP GET ---------------------------------------------------------------------------------
    bool HttpGet(const std::string& url, std::vector<uint8_t>& body, DWORD& status, DWORD* err = nullptr)
    {
        body.clear();
        status = 0;
        if (err) *err = 0;
        const std::wstring wurl = Widen(url);
        URL_COMPONENTSW uc{};
        wchar_t host[256] = {}, path[2048] = {};
        uc.dwStructSize = sizeof uc;
        uc.lpszHostName = host;
        uc.dwHostNameLength = 256;
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = 2048;
        uc.dwSchemeLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
        {
            if (err) *err = GetLastError();
            return false;
        }

        HINTERNET ses = WinHttpOpen(L"external-esp/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
        if (!ses)
        {
            if (err) *err = GetLastError();
            return false;
        }
        WinHttpSetTimeouts(ses, 4000, 5000, 8000, 8000);
        HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
        HINTERNET req = con
                            ? WinHttpOpenRequest(con, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                            : nullptr;
        bool ok = false;
        if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(req, nullptr))
        {
            DWORD sz = sizeof status;
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
            for (;;)
            {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
                const size_t old = body.size();
                body.resize(old + avail);
                DWORD got = 0;
                if (!WinHttpReadData(req, body.data() + old, avail, &got))
                {
                    body.resize(old);
                    break;
                }
                body.resize(old + got);
                if (body.size() > 4u << 20) break; // an avatar is a few KB; refuse anything silly
            }
            ok = true;
        }
        if (!ok && err) *err = GetLastError();
        if (req) WinHttpCloseHandle(req);
        if (con) WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return ok;
    }

    // ---- WIC PNG decode ------------------------------------------------------------------------------
    std::atomic<uint64_t> g_nextId{1};

    bool DecodePng(const std::vector<uint8_t>& data, Image& out)
    {
        if (data.size() < 16) return false;
        ComPtr<IWICImagingFactory> f;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    __uuidof(IWICImagingFactory), reinterpret_cast<void**>(f.put())))) return false;
        ComPtr<IWICStream> stream;
        if (FAILED(f->CreateStream(stream.put())) || FAILED(
            stream->InitializeFromMemory(const_cast<BYTE*>(data.data()), static_cast<DWORD>(data.size())))) return
            false;
        ComPtr<IWICBitmapDecoder> dec;
        if (FAILED(f->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad, dec.put()))) return
            false;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(dec->GetFrame(0, frame.put()))) return false;
        ComPtr<IWICFormatConverter> conv;
        if (FAILED(f->CreateFormatConverter(conv.put())) ||
            FAILED(conv->Initialize(frame.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                    WICBitmapPaletteTypeCustom)))
            return false;
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        if (w == 0 || h == 0 || w > 1024 || h > 1024) return false;
        out.w = static_cast<int>(w);
        out.h = static_cast<int>(h);
        out.bgra.resize(static_cast<size_t>(w) * h * 4);
        if (FAILED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.bgra.size()), out.bgra.data()))) return false;
        out.id = g_nextId++;
        return true;
    }

    // 64x64 (or legacy 64x32) skin -> 16x16 face: head front (8,8,8x8) with the hat layer (40,8) composited over it, 2x nearest.
    bool FaceFromSkin(const Image& skin, Image& out)
    {
        if (skin.w < 64 || skin.h < 32) return false;
        auto px = [&](int x, int y) { return &skin.bgra[(static_cast<size_t>(y) * skin.w + x) * 4]; };
        out.w = out.h = 16;
        out.bgra.assign(16 * 16 * 4, 0);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
            {
                const uint8_t* b = px(8 + x, 8 + y);
                const uint8_t* hat = px(40 + x, 8 + y);
                uint8_t o[4];
                const int inv = 255 - hat[3]; // premultiplied "over": hat + base * (1 - hatAlpha)
                for (int c = 0; c < 4; ++c) o[c] = static_cast<uint8_t>(std::min(255, hat[c] + b[c] * inv / 255));
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) std::copy(o, o + 4, &out.bgra[((y * 2 + dy) * 16 + x * 2 + dx) * 4]);
            }
        out.id = g_nextId++;
        return true;
    }

    // ---- tiny helpers for the Mojang JSON (nested arrays are beyond json.h; these are exact-key scans) -----------
    std::string JsonStringAfter(const std::string& text, const std::string& key, size_t from = 0)
    {
        const std::string pat = "\"" + key + "\"";
        size_t k = text.find(pat, from);
        if (k == std::string::npos) return {};
        size_t q = text.find('"', text.find(':', k + pat.size()) + 1);
        if (q == std::string::npos) return {};
        size_t e = text.find('"', q + 1);
        return e == std::string::npos ? std::string{} : text.substr(q + 1, e - q - 1);
    }

    std::vector<uint8_t> Base64Decode(const std::string& in)
    {
        static const std::string tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::vector<uint8_t> out;
        int val = 0, bits = -8;
        for (unsigned char c : in)
        {
            size_t p = tbl.find(static_cast<char>(c));
            if (p == std::string::npos) continue; // skips '=', whitespace
            val = (val << 6) + static_cast<int>(p);
            bits += 6;
            if (bits >= 0)
            {
                out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
                bits -= 8;
            }
        }
        return out;
    }

    bool IsOnlineUuid(const std::string& u) { return u.size() == 36 && u[8] == '-' && u[14] == '4'; }
    // version nibble 4 = random (Mojang accounts)
    std::string StripDashes(std::string u)
    {
        u.erase(std::remove(u.begin(), u.end(), '-'), u.end());
        return u;
    }

    fs::path CacheDir()
    {
        wchar_t tmp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmp);
        return fs::path(tmp) / "mc_esp_avatars";
    }

    // ---- state ----------------------------------------------------------------------------------------
    struct Entry
    {
        std::shared_ptr<const Image> img;
        bool loading = false;
        Clock::time_point retryAt{};
        std::string uuidHint;
    };

    std::mutex g_mtx;
    std::condition_variable g_cv;
    std::map<std::string, Entry> g_entries; // key: lower-case name
    std::deque<std::string> g_queue; // lower-case names (original spelling kept in g_names)
    std::map<std::string, std::string> g_names;
    std::jthread g_thread;
    bool g_started = false;

    std::shared_ptr<const Image> Fetch(const std::string& name, const std::string& hint)
    {
        const std::string mojang = Env("ESP_MOJANG_API", "https://api.mojang.com");
        const std::string crafatar = Env("ESP_CRAFATAR", "https://crafatar.com");
        const std::string session = Env("ESP_SESSION_API", "https://sessionserver.mojang.com");
        std::vector<uint8_t> body;
        DWORD status = 0;

        // 1. uuid
        std::string uuid;
        if (IsOnlineUuid(hint)) uuid = StripDashes(hint);
        else
        {
            DWORD err = 0;
            const std::string url = mojang + "/users/profiles/minecraft/" + name;
            if (HttpGet(url, body, status, &err) && status == 200)
            {
                auto obj = json::ParseObject(std::string(body.begin(), body.end()));
                if (obj) uuid = json::GetString(*obj, "id", "");
            }
            else
            {
                LOG_INFO("SkinCache: GET %s -> HTTP status %lu, WinHTTP error %lu", url.c_str(),
                         static_cast<unsigned long>(status), static_cast<unsigned long>(err));
            }
        }
        if (uuid.empty())
        {
            LOG_INFO("SkinCache: no Mojang profile for '%s' (offline player?)", name.c_str());
            return nullptr;
        }

        // 2. disk cache
        std::error_code ec;
        const fs::path cache = CacheDir() / (uuid + ".png");
        if (fs::exists(cache, ec))
        {
            std::ifstream f(cache, std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            auto img = std::make_shared<Image>();
            if (DecodePng(bytes, *img)) return img;
        }

        // 3. Crafatar (face + hat layer)
        if (HttpGet(crafatar + "/avatars/" + uuid + "?size=16&overlay=true", body, status) && status == 200)
        {
            auto img = std::make_shared<Image>();
            if (DecodePng(body, *img))
            {
                fs::create_directories(CacheDir(), ec);
                std::ofstream f(cache, std::ios::binary);
                f.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
                LOG_INFO("SkinCache: '%s' -> avatar from Crafatar (%dx%d)", name.c_str(), img->w, img->h);
                return img;
            }
        }

        // 4. Mojang session server -> skin texture -> face
        if (HttpGet(session + "/session/minecraft/profile/" + uuid, body, status) && status == 200)
        {
            const std::string text(body.begin(), body.end());
            const std::string value = JsonStringAfter(text, "value");
            const auto decoded = Base64Decode(value);
            const std::string tex(decoded.begin(), decoded.end());
            const size_t skinPos = tex.find("\"SKIN\"");
            const std::string url = skinPos == std::string::npos ? std::string{} : JsonStringAfter(tex, "url", skinPos);
            if (!url.empty() && HttpGet(url, body, status) && status == 200)
            {
                Image skin;
                auto face = std::make_shared<Image>();
                if (DecodePng(body, skin) && FaceFromSkin(skin, *face))
                {
                    LOG_INFO("SkinCache: '%s' -> face cropped from the skin texture", name.c_str());
                    return face;
                }
            }
        }
        LOG_WARN("SkinCache: could not fetch an avatar for '%s'", name.c_str());
        return nullptr;
    }

    void Worker(std::stop_token st)
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        while (!st.stop_requested())
        {
            std::string key, name, hint;
            {
                std::unique_lock<std::mutex> lk(g_mtx);
                g_cv.wait_for(lk, std::chrono::milliseconds(250),
                              [&] { return !g_queue.empty() || st.stop_requested(); });
                if (g_queue.empty()) continue;
                key = g_queue.front();
                g_queue.pop_front();
                name = g_names[key];
                hint = g_entries[key].uuidHint;
            }
            std::shared_ptr<const Image> img;
            try { img = Fetch(name, hint); }
            catch (const std::exception& e) { LOG_ERROR("SkinCache: %s", e.what()); }
            std::lock_guard<std::mutex> lk(g_mtx);
            Entry& e = g_entries[key];
            e.loading = false;
            e.img = img;
            if (!img) e.retryAt = Clock::now() + std::chrono::seconds(60); // same cooldown as the C# tool
        }
        CoUninitialize();
    }
} // namespace

namespace SkinCache
{
    void Start()
    {
        if (g_started) return;
        g_started = true;
        g_thread = std::jthread([](std::stop_token st) { Worker(st); });
    }

    void Stop()
    {
        if (!g_started) return;
        g_thread.request_stop();
        g_cv.notify_all();
        if (g_thread.joinable()) g_thread.join();
        g_started = false;
    }

    void Request(const std::string& name, const std::string& uuid)
    {
        if (name.empty() || name == "?") return;
        const std::string key = Lower(name);
        std::lock_guard<std::mutex> lk(g_mtx);
        Entry& e = g_entries[key];
        if (e.img || e.loading || Clock::now() < e.retryAt) return;
        e.loading = true;
        if (!uuid.empty() && uuid != "-") e.uuidHint = uuid;
        g_names[key] = name;
        g_queue.push_back(key);
        g_cv.notify_one();
    }

    std::shared_ptr<const Image> Get(const std::string& name)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = g_entries.find(Lower(name));
        return it == g_entries.end() ? nullptr : it->second.img;
    }
} // namespace SkinCache
