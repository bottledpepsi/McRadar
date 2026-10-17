// RaiiHandle.h - RAII owners for Win32 / GDI / COM resources.
#pragma once
#include "Platform.h"
#include <utility>

// Kernel HANDLE (process handles, pipes, snapshots). NULL and INVALID_HANDLE_VALUE both mean "empty".
class UniqueHandle
{
public:
    UniqueHandle() = default;

    explicit UniqueHandle(HANDLE h) noexcept : h_(h)
    {
    }

    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& o) noexcept : h_(std::exchange(o.h_, nullptr))
    {
    }

    UniqueHandle& operator=(UniqueHandle&& o) noexcept
    {
        if (this != &o)
        {
            reset();
            h_ = std::exchange(o.h_, nullptr);
        }
        return *this;
    }

    bool valid() const noexcept { return h_ != nullptr && h_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const noexcept { return h_; }

    void reset(HANDLE next = nullptr) noexcept
    {
        if (valid()) CloseHandle(h_);
        h_ = next;
    }

private:
    HANDLE h_ = nullptr;
};

// Minimal COM smart pointer (no ATL/WRL dependency under MinGW).
template <class T>
class ComPtr
{
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& o) noexcept : p_(std::exchange(o.p_, nullptr))
    {
    }

    ComPtr& operator=(ComPtr&& o) noexcept
    {
        if (this != &o)
        {
            reset();
            p_ = std::exchange(o.p_, nullptr);
        }
        return *this;
    }

    T* get() const noexcept { return p_; }
    T* operator->() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }

    T** put() noexcept
    {
        reset();
        return &p_;
    } // for "out" parameters
    void reset() noexcept
    {
        if (p_)
        {
            p_->Release();
            p_ = nullptr;
        }
    }

private:
    T* p_ = nullptr;
};

// Window-DC / memory-DC / DIB owners used by the overlay surface.
class ScreenDC
{
public:
    ScreenDC() : dc_(GetDC(nullptr))
    {
    }

    ~ScreenDC() { if (dc_) ReleaseDC(nullptr, dc_); }
    ScreenDC(const ScreenDC&) = delete;
    ScreenDC& operator=(const ScreenDC&) = delete;
    HDC get() const noexcept { return dc_; }

private:
    HDC dc_;
};
