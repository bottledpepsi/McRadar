// RaiiHandle.h - RAII wrappers for Win32 / GDI resources.
// Every handle we open is owned by one of these, so early returns, exceptions and
// shutdown paths can never leak a HANDLE, HDC or GDI object.
#pragma once
#include "Platform.h"
#include <utility>

// Owns a kernel HANDLE (process handles, toolhelp snapshots, ...).
class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE h) noexcept : h_(h) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& o) noexcept : h_(std::exchange(o.h_, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& o) noexcept {
        if (this != &o) { reset(); h_ = std::exchange(o.h_, nullptr); }
        return *this;
    }

    // Note: CreateToolhelp32Snapshot returns INVALID_HANDLE_VALUE on failure, OpenProcess
    // returns NULL. valid() treats both as "no handle".
    bool valid() const noexcept { return h_ != nullptr && h_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const noexcept { return h_; }
    void reset(HANDLE next = nullptr) noexcept {
        if (valid()) CloseHandle(h_);
        h_ = next;
    }

private:
    HANDLE h_ = nullptr;
};

// Owns a GDI object (HPEN, HBRUSH, HFONT, HBITMAP). Deleted with DeleteObject.
template <class T>
class GdiObject {
public:
    GdiObject() = default;
    explicit GdiObject(T h) noexcept : h_(h) {}
    ~GdiObject() { if (h_) DeleteObject(h_); }
    GdiObject(const GdiObject&) = delete;
    GdiObject& operator=(const GdiObject&) = delete;
    T get() const noexcept { return h_; }
    explicit operator bool() const noexcept { return h_ != nullptr; }
private:
    T h_ = nullptr;
};

// Owns a memory device context created with CreateCompatibleDC.
class MemDC {
public:
    explicit MemDC(HDC like) noexcept : dc_(CreateCompatibleDC(like)) {}
    ~MemDC() { if (dc_) DeleteDC(dc_); }
    MemDC(const MemDC&) = delete;
    MemDC& operator=(const MemDC&) = delete;
    HDC get() const noexcept { return dc_; }
private:
    HDC dc_;
};

// Selects a GDI object into a DC and restores the previous one on scope exit.
class SelectGuard {
public:
    SelectGuard(HDC dc, HGDIOBJ obj) noexcept : dc_(dc), old_(SelectObject(dc, obj)) {}
    ~SelectGuard() { if (old_) SelectObject(dc_, old_); }
    SelectGuard(const SelectGuard&) = delete;
    SelectGuard& operator=(const SelectGuard&) = delete;
private:
    HDC dc_;
    HGDIOBJ old_;
};
