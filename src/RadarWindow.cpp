#include "RadarWindow.h"
#include "Logger.h"
#include "RadarMath.h"
#include "RaiiHandle.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr wchar_t kClassName[] = L"McRadarWindowClass";
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kFrameMs = 16;  // ~60 FPS repaint, fully independent of the 25 Hz memory polling

constexpr COLORREF kBg      = RGB(14, 16, 20);
constexpr COLORREF kDisc    = RGB(20, 38, 28);
constexpr COLORREF kRing    = RGB(52, 96, 66);
constexpr COLORREF kText    = RGB(190, 220, 200);
constexpr COLORREF kLocal   = RGB(40, 120, 255);   // blue
constexpr COLORREF kEntity  = RGB(235, 40, 40);    // red
}  // namespace

RadarWindow::RadarWindow(HINSTANCE inst, SharedState& state, const Config& cfg)
    : inst_(inst), state_(state), range_(cfg.radarRange), rotate_(cfg.rotateWithPlayer), clamp_(cfg.clampToEdge) {}

RadarWindow::~RadarWindow() {
    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
}

bool RadarWindow::Create() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &RadarWindow::WndProcThunk;
    wc.hInstance = inst_;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512) /* IDC_ARROW */);
    wc.hbrBackground = nullptr;            // we paint every pixel ourselves (no flicker)
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LOG_ERROR("RegisterClassEx failed (%lu)", static_cast<unsigned long>(GetLastError()));
        return false;
    }

    RECT r{0, 0, 420, 420};   // client size
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd_ = CreateWindowExW(0, kClassName, L"MC Radar", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                            nullptr, nullptr, inst_, this /* -> WM_NCCREATE */);
    if (!hwnd_) {
        LOG_ERROR("CreateWindowEx failed (%lu)", static_cast<unsigned long>(GetLastError()));
        return false;
    }
    SetTimer(hwnd_, kTimerId, kFrameMs, nullptr);
    return true;
}

int RadarWindow::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// Win32 wants a free function; stash `this` in the window's user data on creation.
LRESULT CALLBACK RadarWindow::WndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
    RadarWindow* self = nullptr;
    if (m == WM_NCCREATE) {
        self = static_cast<RadarWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = h;
    } else {
        self = reinterpret_cast<RadarWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    return self ? self->WndProc(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT RadarWindow::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_ERASEBKGND:
            return 1;  // handled in Paint via double buffering
        case WM_PAINT:
            Paint();
            return 0;
        case WM_TIMER:
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_MOUSEWHEEL: {   // wheel up = zoom in (smaller range)
            const double notches = GET_WHEEL_DELTA_WPARAM(w) / static_cast<double>(WHEEL_DELTA);
            range_ = std::clamp(range_ / std::pow(1.15, notches), 8.0, 1024.0);
            return 0;
        }
        case WM_KEYDOWN:
            switch (w) {
                case VK_ESCAPE: DestroyWindow(h); break;
                case 'R': rotate_ = !rotate_; break;
                case 'C': clamp_ = !clamp_; break;
                case 'T': ToggleTopmost(); break;
                case VK_ADD: case VK_OEM_PLUS:   range_ = std::clamp(range_ / 1.25, 8.0, 1024.0); break;
                case VK_SUBTRACT: case VK_OEM_MINUS: range_ = std::clamp(range_ * 1.25, 8.0, 1024.0); break;
                default: break;
            }
            return 0;
        case WM_DESTROY:
            KillTimer(h, kTimerId);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(h, m, w, l);
    }
}

void RadarWindow::ToggleTopmost() {
    topmost_ = !topmost_;
    SetWindowPos(hwnd_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

void RadarWindow::Paint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w > 0 && h > 0) {
        // Double buffering: draw into an off-screen bitmap, then blit once.
        MemDC mem(hdc);
        GdiObject<HBITMAP> bmp(CreateCompatibleBitmap(hdc, w, h));
        if (mem.get() && bmp) {
            SelectGuard selBmp(mem.get(), bmp.get());
            Render(mem.get(), w, h, state_.Get());   // copy of the latest snapshot
            BitBlt(hdc, 0, 0, w, h, mem.get(), 0, 0, SRCCOPY);
        }
    }
    EndPaint(hwnd_, &ps);
}

void RadarWindow::Render(HDC dc, int w, int h, const WorldSnapshot& snap) {
    // GDI objects are RAII-owned; created per frame (cheap) so there is no lifetime to manage.
    GdiObject<HBRUSH> bgBrush(CreateSolidBrush(kBg)), discBrush(CreateSolidBrush(kDisc)),
        localBrush(CreateSolidBrush(kLocal)), entBrush(CreateSolidBrush(kEntity));
    GdiObject<HPEN> ringPen(CreatePen(PS_SOLID, 1, kRing)), nullPen(CreatePen(PS_NULL, 0, 0)),
        localPen(CreatePen(PS_SOLID, 2, kLocal)), entPen(CreatePen(PS_SOLID, 1, RGB(120, 10, 10)));
    GdiObject<HFONT> font(CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      FIXED_PITCH | FF_MODERN, L"Consolas"));

    // 1) Background
    RECT full{0, 0, w, h};
    FillRect(dc, &full, bgBrush.get());

    // 2) Radar geometry. Disc is centred and fits the smaller client dimension.
    const int cx = w / 2, cy = h / 2;
    const int R = std::max(20, std::min(w, h) / 2 - 12);
    // World -> screen scale: `range_` blocks span radius R pixels.
    const double pxPerBlock = R / range_;

    {   // disc
        SelectGuard p(dc, ringPen.get()), b(dc, discBrush.get());
        Ellipse(dc, cx - R, cy - R, cx + R + 1, cy + R + 1);
    }
    {   // range rings + crosshair (hollow)
        SelectGuard p(dc, ringPen.get()), b(dc, GetStockObject(NULL_BRUSH));
        for (int i = 1; i <= 2; ++i) {
            const int rr = R * i / 3;
            Ellipse(dc, cx - rr, cy - rr, cx + rr + 1, cy + rr + 1);
        }
        MoveToEx(dc, cx - R, cy, nullptr); LineTo(dc, cx + R, cy);
        MoveToEx(dc, cx, cy - R, nullptr); LineTo(dc, cx, cy + R);
    }

    SetBkMode(dc, TRANSPARENT);
    SelectGuard fontSel(dc, font.get());
    SetTextColor(dc, kText);

    // Heading-up needs a heading. If the agent could not find one, fall back to north-up for
    // this frame instead of drawing a wrong rotation.
    const bool rot = rotate_ && snap.localValid && snap.local.yawValid;

    // 3) Other PLAYERS (red) with their names. Skipped until we know where the local player is.
    int drawn = 0;
    if (snap.localValid) {
        SelectGuard p(dc, entPen.get()), b(dc, entBrush.get());
        for (const PlayerState& e : snap.others) {
            Vec2 off = WorldToRadarOffset(snap.local.pos, snap.local.yawDeg, e.pos, pxPerBlock, rot);
            double dist = std::hypot(off.x, off.y);
            if (dist > R - 3) {
                if (!clamp_) continue;                 // outside radar range
                const double k = (R - 3) / dist;       // pin to the rim, keep bearing
                off.x *= k; off.y *= k;
            }
            const int ex = cx + static_cast<int>(std::lround(off.x));
            const int ey = cy + static_cast<int>(std::lround(off.y));
            Ellipse(dc, ex - 4, ey - 4, ex + 5, ey + 5);
            if (!e.name.empty()) TextOutA(dc, ex + 7, ey - 8, e.name.c_str(), static_cast<int>(e.name.size()));
            ++drawn;
        }
    }

    // 4) Local player (blue) at the exact centre + heading tick.
    if (snap.localValid) {
        // Heading on screen: straight up when the map rotates with the player; otherwise the
        // world-space forward vector (-sin yaw, +cos yaw) in (x, z) maps directly to screen (x, y).
        double hx = 0.0, hy = -1.0;
        const bool drawHeading = snap.local.yawValid;
        if (!rot && snap.local.yawValid) {
            const double yaw = snap.local.yawDeg * kDegToRad;
            hx = -std::sin(yaw);
            hy = std::cos(yaw);
        }
        if (drawHeading) {
            SelectGuard p(dc, localPen.get());
            MoveToEx(dc, cx, cy, nullptr);
            LineTo(dc, cx + static_cast<int>(std::lround(hx * 16)), cy + static_cast<int>(std::lround(hy * 16)));
        }
        SelectGuard p(dc, nullPen.get()), b(dc, localBrush.get());
        Ellipse(dc, cx - 5, cy - 5, cx + 6, cy + 6);
    }

    // Compass hint. In heading-up mode the world's north rotates around the rim; we get its
    // direction by pushing the world vector "one block north" (dz = -1) through the same
    // transform used for entities, so the two can never disagree.
    if (rot) {
        const Vec2 n = WorldToRadarOffset(Vec3{}, snap.local.yawDeg, Vec3{0.0, 0.0, -1.0}, 1.0, true);
        TextOutA(dc, cx + static_cast<int>(std::lround(n.x * (R - 10))) - 4,
                 cy + static_cast<int>(std::lround(n.y * (R - 10))) - 8, "N", 1);
    } else {
        TextOutA(dc, cx - 4, cy - R + 2, "N", 1);
    }

    // 5) HUD text
    char line[200];
    int y = 4;
    auto put = [&](const char* s) { TextOutA(dc, 6, y, s, static_cast<int>(std::strlen(s))); y += 16; };
    put(snap.status.c_str());
    if (snap.localValid) {
        char yawText[24] = "yaw n/a";
        if (snap.local.yawValid) std::snprintf(yawText, sizeof yawText, "yaw %.0f", snap.local.yawDeg);
        std::snprintf(line, sizeof line, "%s%s%.1f %.1f %.1f  %s", snap.local.name.c_str(),
                      snap.local.name.empty() ? "" : ": ", snap.local.pos.x, snap.local.pos.y, snap.local.pos.z, yawText);
        put(line);
    }
    std::snprintf(line, sizeof line, "range %.0f  players shown %d/%llu  %s", range_, drawn,
                  static_cast<unsigned long long>(snap.others.size()),
                  rot ? "heading-up" : (rotate_ ? "north-up (no yaw)" : "north-up"));
    put(line);
    y = h - 20;
    put("wheel/+/-: zoom  R: rotate  C: clamp  T: topmost  Esc: quit");
}
