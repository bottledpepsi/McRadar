#include "Gfx.h"
#include "Logger.h"
#include <algorithm>

namespace
{
    std::wstring ToWide(const std::string& s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }

    D2D1_COLOR_F ToD2D(Col c) { return D2D1_COLOR_F{c.r, c.g, c.b, c.a}; }
    D2D1_RECT_F RectF(float x, float y, float w, float h) { return D2D1_RECT_F{x, y, x + w, y + h}; }

    struct FontSpec
    {
        float size;
        DWRITE_FONT_WEIGHT weight;
    };

    const FontSpec kFonts[] = {
        {10.f, DWRITE_FONT_WEIGHT_SEMI_BOLD}, // Name10
        {13.f, DWRITE_FONT_WEIGHT_SEMI_BOLD}, // Name13
        {17.f, DWRITE_FONT_WEIGHT_SEMI_BOLD}, // Name17
        {12.f, DWRITE_FONT_WEIGHT_NORMAL}, // Ui
        {11.f, DWRITE_FONT_WEIGHT_NORMAL}, // UiSmall
        {12.f, DWRITE_FONT_WEIGHT_BOLD}, // UiBold
        {10.f, DWRITE_FONT_WEIGHT_NORMAL}, // Hint
    };

    // First installed family from a preference list (Segoe UI is missing on some stripped-down Windows
    // installs and under Wine). Returns the last candidate if none is found.
    std::wstring PickFamily(IDWriteFactory* dw)
    {
        static const wchar_t* const kCandidates[] = {
            L"Segoe UI", L"Tahoma", L"Arial", L"Verdana", L"DejaVu Sans", L"Liberation Sans"
        };
        ComPtr<IDWriteFontCollection> coll;
        if (FAILED(dw->GetSystemFontCollection(coll.put(), FALSE))) return kCandidates[0];
        for (const wchar_t* name : kCandidates)
        {
            UINT32 idx = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(coll->FindFamilyName(name, &idx, &exists)) && exists) return name;
        }
        return kCandidates[0];
    }
} // namespace

Gfx::~Gfx() { FreeSurface(); }

bool Gfx::Init()
{
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
                                   reinterpret_cast<void**>(factory_.put()));
    if (FAILED(hr))
    {
        LOG_ERROR("D2D1CreateFactory failed (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(dw_.put()));
    if (FAILED(hr))
    {
        LOG_ERROR("DWriteCreateFactory failed (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }

    const std::wstring family = PickFamily(dw_.get());
    LOG_INFO("UI font family: %ls", family.c_str());
    for (int f = 0; f < static_cast<int>(Font::Count); ++f)
    {
        for (int a = 0; a < 3; ++a)
        {
            hr = dw_->CreateTextFormat(family.c_str(), nullptr, kFonts[f].weight, DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL, kFonts[f].size, L"en-us", fmt_[f][a].put());
            if (FAILED(hr))
            {
                LOG_ERROR("CreateTextFormat failed (0x%08lX)", static_cast<unsigned long>(hr));
                return false;
            }
            fmt_[f][a]->SetTextAlignment(a == 0
                                             ? DWRITE_TEXT_ALIGNMENT_LEADING
                                             : a == 1
                                             ? DWRITE_TEXT_ALIGNMENT_CENTER
                                             : DWRITE_TEXT_ALIGNMENT_TRAILING);
            fmt_[f][a]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
    return CreateRenderTarget();
}

bool Gfx::CreateRenderTarget()
{
    bitmaps_.clear(); // device resources belong to the old render target
    brush_.reset();
    rt_.reset();
    D2D1_RENDER_TARGET_PROPERTIES p{};
    p.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    p.pixelFormat = D2D1_PIXEL_FORMAT{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}; // per-pixel alpha
    p.dpiX = 96.f;
    p.dpiY = 96.f;
    p.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    p.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
    HRESULT hr = factory_->CreateDCRenderTarget(&p, rt_.put());
    if (FAILED(hr))
    {
        LOG_ERROR("CreateDCRenderTarget failed (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }
    // Grayscale AA: ClearType needs an opaque background, which a transparent overlay does not have.
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    D2D1_COLOR_F white{1, 1, 1, 1};
    hr = rt_->CreateSolidColorBrush(white, brush_.put());
    return SUCCEEDED(hr);
}

void Gfx::FreeSurface()
{
    if (memDc_)
    {
        if (oldBmp_) SelectObject(memDc_, oldBmp_);
        oldBmp_ = nullptr;
        if (dib_) DeleteObject(dib_);
        dib_ = nullptr;
        DeleteDC(memDc_);
        memDc_ = nullptr;
    }
}

bool Gfx::Resize(int w, int h)
{
    if (w <= 0 || h <= 0) return false;
    if (w == w_ && h == h_ && dib_ && rt_) return true;
    FreeSurface();
    ScreenDC screen;
    memDc_ = CreateCompatibleDC(screen.get());
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // negative = top-down rows
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    dib_ = CreateDIBSection(screen.get(), &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib_ || !memDc_)
    {
        LOG_ERROR("CreateDIBSection failed (%lu)", static_cast<unsigned long>(GetLastError()));
        FreeSurface();
        return false;
    }
    oldBmp_ = static_cast<HBITMAP>(SelectObject(memDc_, dib_));
    w_ = w;
    h_ = h;
    if (!rt_ && !CreateRenderTarget()) return false;
    LOG_INFO("Overlay surface %dx%d", w, h);
    return true;
}

void Gfx::Begin(Col clear)
{
    RECT rc{0, 0, w_, h_};
    rt_->BindDC(memDc_, &rc);
    rt_->BeginDraw();
    D2D1_MATRIX_3X2_F id = D2D1::Matrix3x2F::Identity();
    rt_->SetTransform(&id);
    D2D1_COLOR_F c = ToD2D(clear);
    rt_->Clear(&c);
}

bool Gfx::End()
{
    HRESULT hr = rt_->EndDraw();
    if (hr == static_cast<HRESULT>(D2DERR_RECREATE_TARGET))
    {
        LOG_WARN("D2D render target lost - recreating.");
        w_ = h_ = 0; // force Resize() to rebuild everything
        rt_.reset();
        return false;
    }
    return SUCCEEDED(hr);
}

bool Gfx::Present(HWND hwnd, int x, int y, float opacity)
{
    POINT dst{x, y}, src{0, 0};
    SIZE sz{w_, h_};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, static_cast<BYTE>(std::clamp(opacity, 0.f, 1.f) * 255.f + 0.5f), AC_SRC_ALPHA};
    return UpdateLayeredWindow(hwnd, nullptr, &dst, &sz, memDc_, &src, 0, &bf, ULW_ALPHA) != 0;
}

void Gfx::SetBrush(Col c)
{
    D2D1_COLOR_F d = ToD2D(c);
    brush_->SetColor(&d);
}

void Gfx::Line(float x1, float y1, float x2, float y2, Col c, float t)
{
    SetBrush(c);
    rt_->DrawLine(D2D1_POINT_2F{x1, y1}, D2D1_POINT_2F{x2, y2}, brush_.get(), t);
}

void Gfx::Rect(float x, float y, float w, float h, Col c, float t)
{
    SetBrush(c);
    D2D1_RECT_F r = RectF(x, y, w, h);
    rt_->DrawRectangle(&r, brush_.get(), t);
}

void Gfx::FillRect(float x, float y, float w, float h, Col c)
{
    SetBrush(c);
    D2D1_RECT_F r = RectF(x, y, w, h);
    rt_->FillRectangle(&r, brush_.get());
}

void Gfx::RRect(float x, float y, float w, float h, float r, Col c, float t)
{
    SetBrush(c);
    D2D1_ROUNDED_RECT rr{RectF(x, y, w, h), r, r};
    rt_->DrawRoundedRectangle(&rr, brush_.get(), t);
}

void Gfx::FillRRect(float x, float y, float w, float h, float r, Col c)
{
    SetBrush(c);
    D2D1_ROUNDED_RECT rr{RectF(x, y, w, h), r, r};
    rt_->FillRoundedRectangle(&rr, brush_.get());
}

void Gfx::Circle(float x, float y, float r, Col c, float t)
{
    SetBrush(c);
    D2D1_ELLIPSE e{D2D1_POINT_2F{x, y}, r, r};
    rt_->DrawEllipse(&e, brush_.get(), t);
}

void Gfx::FillCircle(float x, float y, float r, Col c)
{
    SetBrush(c);
    D2D1_ELLIPSE e{D2D1_POINT_2F{x, y}, r, r};
    rt_->FillEllipse(&e, brush_.get());
}

void Gfx::FillTriangle(float x1, float y1, float x2, float y2, float x3, float y3, Col c)
{
    ComPtr<ID2D1PathGeometry> g;
    if (FAILED(factory_->CreatePathGeometry(g.put()))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(g->Open(sink.put()))) return;
    sink->BeginFigure(D2D1_POINT_2F{x1, y1}, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1_POINT_2F{x2, y2});
    sink->AddLine(D2D1_POINT_2F{x3, y3});
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    SetBrush(c);
    rt_->FillGeometry(g.get(), brush_.get());
}

void Gfx::DrawImage(const Image& img, float x, float y, float w, float h, float opacity)
{
    if (img.bgra.empty() || img.w <= 0 || img.h <= 0) return;
    auto it = bitmaps_.find(img.id);
    if (it == bitmaps_.end())
    {
        if (bitmaps_.size() > 128) bitmaps_.clear();
        D2D1_BITMAP_PROPERTIES bp{};
        bp.pixelFormat = D2D1_PIXEL_FORMAT{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
        bp.dpiX = bp.dpiY = 96.f;
        ComPtr<ID2D1Bitmap> bmp;
        if (FAILED(rt_->CreateBitmap(D2D1_SIZE_U{static_cast<UINT32>(img.w), static_cast<UINT32>(img.h)},
                                     img.bgra.data(),
                                     static_cast<UINT32>(img.w) * 4, &bp, bmp.put())))
            return;
        it = bitmaps_.emplace(img.id, std::move(bmp)).first;
    }
    D2D1_RECT_F d = RectF(x, y, w, h);
    rt_->DrawBitmap(it->second.get(), &d, opacity, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, nullptr);
}

void Gfx::PushClip(float x, float y, float w, float h)
{
    D2D1_RECT_F r = RectF(x, y, w, h);
    rt_->PushAxisAlignedClip(&r, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Gfx::PopClip() { rt_->PopAxisAlignedClip(); }

// align: 0 left (text starts at x), 1 centered on x, 2 right edge at x. The layout rect is 400 px wide.
void Gfx::DrawStr(const std::wstring& ws, float x, float y, float w, Font f, Col c, int align, bool shadow)
{
    if (ws.empty()) return;
    float left = align == 0 ? x : align == 1 ? x - w * 0.5f : x - w;
    IDWriteTextFormat* fmt = fmt_[static_cast<int>(f)][align].get();
    if (shadow)
    {
        SetBrush(Col{0, 0, 0, 0.85f * c.a});
        D2D1_RECT_F rs = RectF(left + 1, y + 1, w, 40);
        rt_->DrawText(ws.c_str(), static_cast<UINT32>(ws.size()), fmt, &rs, brush_.get());
    }
    SetBrush(c);
    D2D1_RECT_F r = RectF(left, y, w, 40);
    rt_->DrawText(ws.c_str(), static_cast<UINT32>(ws.size()), fmt, &r, brush_.get());
}

void Gfx::Text(const std::string& s, float x, float y, Font f, Col c, bool sh)
{
    DrawStr(ToWide(s), x, y, 400.f, f, c, 0, sh);
}

void Gfx::TextCentered(const std::string& s, float cx, float y, Font f, Col c, bool sh)
{
    DrawStr(ToWide(s), cx, y, 400.f, f, c, 1, sh);
}

void Gfx::TextRight(const std::string& s, float rx, float y, Font f, Col c, bool sh)
{
    DrawStr(ToWide(s), rx, y, 400.f, f, c, 2, sh);
}

float Gfx::TextWidth(const std::string& s, Font f)
{
    std::wstring ws = ToWide(s);
    if (ws.empty()) return 0.f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dw_->CreateTextLayout(ws.c_str(), static_cast<UINT32>(ws.size()), fmt_[static_cast<int>(f)][0].get(),
                                     1000.f, 40.f, layout.put()))) return 0.f;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}
