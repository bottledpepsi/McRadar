// Platform.h - one place for Windows header configuration.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
#include <cstdint>

static_assert(sizeof(void*) == 8,
              "Build this as a 64-bit (x64) executable: modern Minecraft runs on a 64-bit JVM "
              "and a 32-bit process cannot reliably read a 64-bit process's address space.");
