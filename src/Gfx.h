// Gfx.h - Direct2D + DirectWrite drawing onto a per-pixel-alpha layered window.
//
// Why this API: an external overlay must be transparent and click-through, and must not inject into
// (or hook) the game's renderer. A layered window + UpdateLayeredWindow gives true per-pixel alpha
// over any game window. Direct2D renders anti-aliased primitives and DirectWrite renders text into a
// 32-bit premultiplied-alpha DIB section through a GDI-compatible DC render target; the finished DIB
// is handed to the window manager in one call. It is light on CPU/GPU, has no swap chain to
// configure, and only needs d2d1/dwrite (present on every Windows 7+ machine).
//
// Double buffering: all drawing goes to the off-screen DIB (the back buffer); Present() publishes the
// completed frame atomically via UpdateLayeredWindow, so the screen never shows a half-drawn frame.
// VSync: UpdateLayeredWindow has no swap interval, so frame pacing is done by the caller with
// DwmFlush() right after Present() - it blocks until the desktop compositor's next vblank-aligned pass.
#pragma once
#include "Platform.h"
#include "RaiiHandle.h"
#ifdef DrawText
#undef DrawText          // windows.h maps DrawText -> DrawTextA; that would rename ID2D1RenderTarget::DrawText
#endif
#include <d2d1.h>
#include <dwrite.h>
#include <string>

struct Col
{
    float r = 1, g = 1, b = 1, a = 1;
    Col with(float alpha) const { return {r, g, b, alpha}; }
};

enum class Font { Name10, Name13, Name17, Ui, UiSmall, UiBold, Hint, Count };

class Gfx
{
public:
    Gfx() = default;
    ~Gfx();
    Gfx(const Gfx&) = delete;
    Gfx& operator=(const Gfx&) = delete;

    bool Init(); // factories, text formats
    bool Resize(int w, int h); // (re)create the DIB + render target binding
    int Width() const { return w_; }
    int Height() const { return h_; }

    void Begin(Col clear); // BindDC + BeginDraw + Clear
    bool End(); // EndDraw; false => render target lost (recreated on next Resize)
    // Publishes the frame: screen position x,y, global opacity 0..1 (OverlayOpacity setting).
    bool Present(HWND hwnd, int x, int y, float opacity);

    // ---- primitives (device-independent pixels == screen pixels; the target runs at 96 dpi) ----
    void Line(float x1, float y1, float x2, float y2, Col c, float thick = 1.f);
    void Rect(float x, float y, float w, float h, Col c, float thick = 1.f);
    void FillRect(float x, float y, float w, float h, Col c);
    void RRect(float x, float y, float w, float h, float r, Col c, float thick = 1.f);
    void FillRRect(float x, float y, float w, float h, float r, Col c);
    void Circle(float x, float y, float r, Col c, float thick = 1.f);
    void FillCircle(float x, float y, float r, Col c);
    void FillTriangle(float x1, float y1, float x2, float y2, float x3, float y3, Col c);
    void PushClip(float x, float y, float w, float h);
    void PopClip();

    // ---- text (UTF-8). Left-aligned at x, centered on x, or right-aligned to x. ----
    void Text(const std::string& s, float x, float y, Font f, Col c, bool shadow = false);
    void TextCentered(const std::string& s, float cx, float y, Font f, Col c, bool shadow = false);
    void TextRight(const std::string& s, float rx, float y, Font f, Col c, bool shadow = false);
    float TextWidth(const std::string& s, Font f);

private:
    bool CreateRenderTarget();
    void DrawStr(const std::wstring& ws, float x, float y, float w, Font f, Col c, int align, bool shadow);
    void SetBrush(Col c);
    void FreeSurface();

    ComPtr<ID2D1Factory> factory_;
    ComPtr<ID2D1DCRenderTarget> rt_;
    ComPtr<ID2D1SolidColorBrush> brush_; // single scratch brush, recoloured per draw call
    ComPtr<IDWriteFactory> dw_;
    ComPtr<IDWriteTextFormat> fmt_[static_cast<int>(Font::Count)][3]; // [font][0=left 1=center 2=right]

    HDC memDc_ = nullptr;
    HBITMAP dib_ = nullptr, oldBmp_ = nullptr;
    int w_ = 0, h_ = 0;
};
