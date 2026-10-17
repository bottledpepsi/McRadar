#include "Logger.h"
#include "Paths.h"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace elog
{
    namespace
    {
        constexpr size_t kMaxLines = 2000;
        std::mutex g_mtx;
        std::filesystem::path LogPath() { return AppDir() / "esp_log.txt"; }

        void TrimLog()
        {
            std::ifstream in(LogPath());
            if (!in) return;
            std::vector<std::string> lines;
            for (std::string l; std::getline(in, l);) lines.push_back(std::move(l));
            in.close();
            if (lines.size() <= kMaxLines) return;
            std::ofstream out(LogPath(), std::ios::trunc);
            for (size_t i = lines.size() - kMaxLines; i < lines.size(); ++i) out << lines[i] << '\n';
        }
    } // namespace

    void Write(Level lvl, const char* fmt, ...)
    {
        char msg[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(msg, sizeof msg, fmt, ap);
        va_end(ap);

        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        const int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).
            count() % 1000);
        char ts[32];

        std::lock_guard<std::mutex> lock(g_mtx);
        std::tm tmv = *std::localtime(&t); // guarded by g_mtx
        std::snprintf(ts, sizeof ts, "%02d:%02d:%02d.%03d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
        static const char* const kTag[] = {"INFO ", "WARN ", "ERROR"};
        std::string line = std::string(ts) + " [" + kTag[static_cast<int>(lvl)] + "] " + msg;

        std::fprintf(stdout, "%s\n", line.c_str());
        std::fflush(stdout);
        try
        {
            std::ofstream f(LogPath(), std::ios::app);
            if (f) f << line << '\n';
        }
        catch (...)
        {
            /* nothing useful to do if logging itself fails */
        }
    }

    void Init()
    {
        try { TrimLog(); }
        catch (...)
        {
        }
        Write(Level::Info, "===========================================");
        const std::time_t t = std::time(nullptr);
        char buf[32];
        std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        Write(Level::Info, "external-esp (C++)  session started  %s", buf);
        Write(Level::Info, "===========================================");
    }
} // namespace elog
