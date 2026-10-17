// EspOverlay.h - the transparent, click-through, always-on-top overlay window and the main loop.
#pragma once
#include "AgentInjector.h"
#include "AgentSupervisor.h"
#include "App.h"
#include "Camera.h"
#include "ConfigUi.h"
#include "Demo.h"
#include "EspConfig.h"
#include "EspRenderer.h"
#include "Gfx.h"
#include "Smoother.h"
#include "UdpListener.h"
#include <chrono>
#include <unordered_map>

class EspOverlay
{
public:
    using Clock = std::chrono::steady_clock;

    explicit EspOverlay(const AppOptions& opt);
    ~EspOverlay();
    EspOverlay(const EspOverlay&) = delete;
    EspOverlay& operator=(const EspOverlay&) = delete;

    int Run();

private:
    bool CreateOverlayWindow();
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMessage(HWND, UINT, WPARAM, LPARAM);

    bool GameGeometry(RECT& r) const; // client area of the game window in screen coordinates
    bool KeyEdge(int vk); // rising edge of a globally polled key
    void HandleHotkeys(bool gameOrOverlayFocused);
    void SetClickThrough(bool on);
    void OpenConfig();
    void CloseConfig();
    void ForceForeground(HWND h);
    void SetShown(bool show);
    void RenderFrame(const RECT& r, bool gameFocused);
    void Pace(Clock::time_point frameStart);

    AppOptions opt_;
    Settings settings_;
    Gfx gfx_;
    EspRenderer renderer_;
    ConfigUi ui_;
    UdpListener udp_;
    AgentInjector injector_;
    AgentSupervisor sup_;
    Smoother smoother_;
    DemoSource demo_;

    HWND hwnd_ = nullptr;
    bool shown_ = false, clickThrough_ = true;
    bool visible_ = true; // ESP drawing on/off (toggle hotkey)
    bool configOpen_ = false, quit_ = false, openedAtStart_ = false;
    Clock::time_point configOpenedAt_{};
    Clock::time_point exitHoldStart_{};
    bool exitHolding_ = false;
    std::unordered_map<int, bool> prevKeys_;

    uint64_t lastSeq_ = 0, demoSeq_ = 0;
    Clock::time_point lastDemoPush_{};
    HWND lastGameWnd_ = nullptr;

    // input accumulated by the window procedure, consumed once per frame
    UiInput pend_;

    // fps counter for the HUD
    Clock::time_point fpsWindowStart_{};
    int fpsFrames_ = 0;
    float fps_ = 0;
};
