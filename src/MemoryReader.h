// MemoryReader.h - safe wrappers around ReadProcessMemory.
//
// How it works: ReadProcessMemory(hProcess, address, buffer, size, &bytesRead) asks the kernel
// to copy `size` bytes from the *target's* virtual address space into our buffer. It fails
// (returns FALSE / bytesRead < size, usually error 299 ERROR_PARTIAL_COPY) when any page in the
// range is unmapped - which is exactly what happens for stale/garbage pointers or after the
// target exits. We never dereference target addresses ourselves; they're just integers.
#pragma once
#include "Platform.h"
#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>

class MemoryReader {
public:
    explicit MemoryReader(HANDLE process) noexcept : process_(process) {}

    // Raw copy. Returns true only if the *entire* range was read.
    bool ReadRaw(uintptr_t address, void* out, size_t size) const noexcept {
        if (size == 0) return true;
        if (!process_) { lastError_ = ERROR_INVALID_HANDLE; return false; }
        SIZE_T got = 0;
        if (!ReadProcessMemory(process_, reinterpret_cast<LPCVOID>(address), out, size, &got) ||
            got != size) {
            lastError_ = GetLastError();
            return false;
        }
        return true;
    }

    // Typed read for trivially copyable T (int, float, double, uintptr_t, POD structs).
    template <class T>
    std::optional<T> Read(uintptr_t address) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "Read<T> needs a trivially copyable type");
        T value{};
        if (!ReadRaw(address, &value, sizeof(T))) return std::nullopt;
        return value;
    }

    // Reads a native 8-byte pointer and rejects obviously invalid values (null, kernel range).
    std::optional<uintptr_t> ReadPtr(uintptr_t address) const noexcept {
        auto p = Read<uintptr_t>(address);
        if (!p || !IsPlausiblePointer(*p)) return std::nullopt;
        return p;
    }

    // Follows `offsets`: cur = *(cur + offset) for each entry (see PointerChain in Config.h).
    // Any unreadable / implausible link aborts the whole chain instead of crashing or looping.
    std::optional<uintptr_t> ResolveChain(uintptr_t start, std::span<const uintptr_t> offsets) const noexcept {
        uintptr_t cur = start;
        for (uintptr_t off : offsets) {
            auto next = ReadPtr(cur + off);
            if (!next) return std::nullopt;
            cur = *next;
        }
        return cur;
    }

    // User-mode addresses on x64 Windows are < 0x00007FFF'FFFFFFFF; the bottom 64 KB is never mapped.
    static bool IsPlausiblePointer(uintptr_t p) noexcept {
        return p >= 0x10000ull && p <= 0x00007FFFFFFFFFFFull;
    }

    DWORD LastError() const noexcept { return lastError_; }
    HANDLE Process() const noexcept { return process_; }

private:
    HANDLE process_;
    mutable DWORD lastError_ = 0;
};
