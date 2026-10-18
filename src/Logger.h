// Logger.h - port of EspLogger.cs: esp_log.txt next to the exe (trimmed to 2000 lines at start-up),
// mirrored to the console (this build is a console-subsystem exe: no -mwindows).
#pragma once
#include <cstdarg>

#if defined(__GNUC__)
#define ESP_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define ESP_PRINTF(f, a)
#endif

namespace elog
{
    enum class Level { Info, Warn, Error };

    void Init(); // trim old log, write session banner
    ESP_PRINTF(2, 3) void Write(Level lvl, const char* fmt, ...);
} // namespace elog

#define LOG_INFO(...)  ::elog::Write(::elog::Level::Info, __VA_ARGS__)
#define LOG_WARN(...)  ::elog::Write(::elog::Level::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::elog::Write(::elog::Level::Error, __VA_ARGS__)
