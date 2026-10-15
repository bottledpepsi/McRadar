// RadarWindow.h - native Win32 + GDI renderer for the circular radar.
#pragma once
#include "Config.h"
#include "Platform.h"
#include "Types.h"

class RadarWindow {
public:
    RadarWindow(HINSTANCE inst, SharedState& state, const Config& cfg);
    ~RadarWindow();
    RadarWindow(const RadarWindow&) = delete;
    RadarWindow& operator=(const RadarWindow&) = delete;

    bool Create();
    int Run();   // message loop; returns the process exit code

private:
    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    void Paint();
    void Render(HDC dc, int w, int h, const WorldSnapshot& snap);
    void ToggleTopmost();

    HINSTANCE inst_;
    SharedState& state_;
    HWND hwnd_ = nullptr;

    // Runtime-adjustable view settings (initialised from Config).
    double range_;
    bool rotate_;
    bool clamp_;
    bool topmost_ = false;
};
