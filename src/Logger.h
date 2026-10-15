// Logger.h - tiny thread-safe console logger (printf style, no dependencies).
#pragma once
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

#if defined(__GNUC__)
#define MCR_PRINTF(fmtIdx, argIdx) __attribute__((format(printf, fmtIdx, argIdx)))
#else
#define MCR_PRINTF(fmtIdx, argIdx)
#endif

namespace rlog {

enum class Level { Debug, Info, Warn, Error };

MCR_PRINTF(2, 3)
inline void Write(Level lvl, const char* fmt, ...) {
    static std::mutex mtx;  // worker thread + UI thread both log
    std::lock_guard<std::mutex> lock(mtx);

    static const char* const kTags[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
    std::time_t now = std::time(nullptr);
    char ts[16] = {};
    std::strftime(ts, sizeof ts, "%H:%M:%S", std::localtime(&now));  // guarded by mtx

    std::fprintf(stdout, "[%s] [%s] ", ts, kTags[static_cast<int>(lvl)]);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stdout, fmt, ap);
    va_end(ap);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

}  // namespace rlog

#define LOG_DEBUG(...) ::rlog::Write(::rlog::Level::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::rlog::Write(::rlog::Level::Info, __VA_ARGS__)
#define LOG_WARN(...)  ::rlog::Write(::rlog::Level::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::rlog::Write(::rlog::Level::Error, __VA_ARGS__)
