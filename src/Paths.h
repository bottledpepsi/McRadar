// Paths.h - "next to the executable" (the C# AppContext.BaseDirectory equivalent).
#pragma once
#include <filesystem>
#ifdef _WIN32
#include "Platform.h"
#endif

inline std::filesystem::path AppDir()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof buf / sizeof buf[0]));
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
#else
    return std::filesystem::current_path(); // host-side unit tests
#endif
}
