#include "CandidateWindow.h"
#include "EmbraceMark.h"
#include "LucyMark.h"
#include <windowsx.h>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <gdiplus.h>

HINSTANCE CandidateWindow::s_hInst = nullptr;
std::wstring CandidateWindow::s_className;

namespace {
struct Palette {
    COLORREF surface, ink, muted, line, selected, hover, accent;
    COLORREF selectedInk = CLR_INVALID, selectedMuted = CLR_INVALID;
};
constexpr Palette kLight = {
    RGB(255, 255, 255), RGB(32, 40, 51), RGB(94, 106, 120),
    RGB(224, 230, 237), RGB(232, 241, 255), RGB(245, 248, 252),
    RGB(48, 111, 217)
};
constexpr Palette kDark = {
    RGB(31, 36, 42), RGB(244, 246, 248), RGB(174, 186, 199),
    RGB(62, 73, 85), RGB(38, 62, 88), RGB(43, 53, 64),
    RGB(116, 181, 255)
};
constexpr Palette kTerminal = {
    RGB(0, 0, 0), RGB(245, 245, 245), RGB(180, 180, 180),
    RGB(66, 66, 66), RGB(40, 40, 40), RGB(28, 28, 28), RGB(255, 255, 255)
};
constexpr Palette kGameBoy = {
    RGB(155, 188, 15), RGB(15, 56, 15), RGB(15, 56, 15),
    RGB(48, 98, 48), RGB(139, 172, 15), RGB(139, 172, 15), RGB(15, 56, 15)
};
constexpr Palette kLucy = {
    RGB(13, 10, 24), RGB(242, 237, 250), RGB(190, 194, 215),
    RGB(64, 55, 84), RGB(215, 209, 242), RGB(40, 37, 59), RGB(166, 231, 238),
    RGB(21, 15, 40), RGB(37, 31, 66)
};
constexpr Palette kDavidLucy = {
    RGB(18, 17, 34), RGB(236, 247, 244), RGB(178, 193, 201),
    RGB(67, 49, 90), RGB(149, 243, 184), RGB(43, 30, 62), RGB(149, 243, 184),
    RGB(19, 32, 35), RGB(39, 61, 57)
};
constexpr Palette kDavidLucyEmbrace = {
    kDavidLucy.surface, kDavidLucy.ink, RGB(211, 222, 227),
    kDavidLucy.line, kDavidLucy.selected, kDavidLucy.hover, kDavidLucy.accent,
    kDavidLucy.selectedInk, kDavidLucy.selectedMuted
};
constexpr unsigned kLucyGradient[] = {0xf5e8ff, 0xc4c7f0, 0x7acbef, 0x3cace5, 0x48b9d5, 0x4ed89f, 0x80f1aa};
constexpr float kLucyGradientStops[] = {0, .12f, .3f, .5f, .65f, .82f, 1};

// Lucy's hair: white-lavender through cyan to mint. Letters and highlight share the colours.
constexpr unsigned kLucyHair[] = {0xf5e8ff, 0xc4c7f0, 0x7acbef, 0x4ed89f, 0x80f1aa};
constexpr float kLucyTitleStops[] = {0, .22f, .52f, .8f, 1};
constexpr float kLucyPlateStops[] = {0, .26f, .58f, .86f, 1};
constexpr COLORREF kLucyPicked = RGB(128, 241, 170);
// Lucy's highlight opacity fades from the number to the right edge so the art shows through.
constexpr float kPlateAlphaLeft = 1.0f, kPlateAlphaRight = .55f;

COLORREF GradientColor(const unsigned* colors, const float* stops, int count, float position) {
    const float t = (std::clamp)(position, 0.0f, 1.0f);
    int segment = 0;
    while (segment < count - 2 && t > stops[segment + 1]) ++segment;
    const float blend = (t - stops[segment]) / (stops[segment + 1] - stops[segment]);
    auto channel = [&](int shift) { return static_cast<BYTE>(
        ((colors[segment] >> shift) & 255) * (1 - blend) +
        ((colors[segment + 1] >> shift) & 255) * blend + .5f); };
    return RGB(channel(16), channel(8), channel(0));
}

COLORREF LucyTitleGradientColor(float position) {
    return GradientColor(kLucyHair, kLucyTitleStops, 5, position);
}

bool IsLucyTheme(CandidateTheme theme) {
    return theme == CandidateTheme::Lucy || theme == CandidateTheme::LucyFlow;
}

bool IsLargeEarthTheme(CandidateTheme theme) {
    return theme == CandidateTheme::DavidLucyEmbrace || theme == CandidateTheme::Earth;
}

bool IsDavidLucyTheme(CandidateTheme theme) {
    return theme == CandidateTheme::DavidLucy || IsLargeEarthTheme(theme);
}

bool IsCyberTheme(CandidateTheme theme) {
    return IsLucyTheme(theme) ||
        IsDavidLucyTheme(theme);
}

bool IsIconFreeTheme(CandidateTheme theme) {
    return IsLucyTheme(theme) || IsDavidLucyTheme(theme);
}

// Lucy and David & Lucy share the edgerunner panel: long frame bars, fading highlight, header caret.
bool HasEdgerunnerPanel(CandidateTheme theme) {
    return IsLucyTheme(theme) || IsDavidLucyTheme(theme);
}

bool HasMoonBackground(CandidateTheme theme) {
    return IsDavidLucyTheme(theme);
}

// Detailed vector marks use the full 44x34 header slot.
bool HasWideMark(CandidateTheme theme) {
    return IsDavidLucyTheme(theme) || IsLucyTheme(theme);
}

int LetterIndex(wchar_t ch) {
    if (ch >= L'a' && ch <= L'z') return ch - L'a';
    return ch >= L'A' && ch <= L'Z' ? ch - L'A' : -1;
}

IStream* EmbeddedImageStream(HINSTANCE instance, WORD resourceId) {
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    const DWORD bytes = resource ? SizeofResource(instance, resource) : 0;
    const void* data = bytes ? LockResource(LoadResource(instance, resource)) : nullptr;
    if (!data) return nullptr;
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return nullptr;
    ULONG written = 0;
    LARGE_INTEGER start = {};
    if (FAILED(stream->Write(data, bytes, &written)) || written != bytes || FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr))) {
        stream->Release();
        return nullptr;
    }
    return stream;
}

Gdiplus::Status FillEmbraceFace(Gdiplus::Graphics& graphics, const EmbraceMark::Face& face,
    const Gdiplus::PointF* points, float top, float height, COLORREF surface, float opacity,
    const Gdiplus::GraphicsPath* hairPath = nullptr) {
    auto fade = [opacity](int value, int base) { return static_cast<BYTE>(base + (value - base) * opacity + .5f); };
    auto color = [&](unsigned rgb) { return Gdiplus::Color(255, fade((rgb >> 16) & 255, GetRValue(surface)),
        fade((rgb >> 8) & 255, GetGValue(surface)), fade(rgb & 255, GetBValue(surface))); };
    if (face.hair) {
        // One continuous blue gradient across the whole hair silhouette, with no split shadow plane.
        Gdiplus::Color colors[7];
        for (int i = 0; i < 7; ++i) colors[i] = color(kLucyGradient[i]);
        Gdiplus::LinearGradientBrush brush(Gdiplus::RectF(0, top, 1, height * EmbraceMark::hairHeight),
            colors[0], colors[6], Gdiplus::LinearGradientModeVertical);
        brush.SetInterpolationColors(colors, kLucyGradientStops, 7);
        return hairPath ? graphics.FillPath(&brush, hairPath) :
            graphics.FillPolygon(&brush, points, face.count, Gdiplus::FillModeWinding);
    }
    Gdiplus::SolidBrush brush(color(face.color));
    return graphics.FillPolygon(&brush, points, face.count, Gdiplus::FillModeWinding);
}

bool DrawLargeEarthBackground(Gdiplus::Graphics& graphics, HINSTANCE instance, SIZE size, COLORREF surface, bool includeFigure) {
    IStream* stream = EmbeddedImageStream(instance, 307);
    if (!stream) return false;
    bool okay = false;
    const auto state = graphics.Save();
    {
        Gdiplus::Bitmap earth(stream);
        if (earth.GetLastStatus() == Gdiplus::Ok) {
            // Continue the same photograph and figure behind the transparent page footer.
            graphics.SetClip(Gdiplus::Rect(0, 0, size.cx, size.cy));
            // Keep the oversized globe fixed across pages; expose its lower arc behind the embrace.
            constexpr float diameter = 304, earthTop = -94;
            Gdiplus::ColorMatrix tint = {{{.45f, 0, 0, 0, 0}, {.15f, .95f, .1f, 0, 0},
                {0, .1f, 1, 0, 0}, {0, 0, 0, .32f, 0}, {0, 0, 0, 0, 1}}};
            Gdiplus::ImageAttributes attributes;
            attributes.SetColorMatrix(&tint);
            attributes.SetColorKey(Gdiplus::Color(255, 0, 0, 0), Gdiplus::Color(255, 12, 12, 12));
            okay = graphics.DrawImage(&earth, Gdiplus::RectF((size.cx - diameter) / 2, earthTop, diameter, diameter),
                0, 0, static_cast<float>(earth.GetWidth()), static_cast<float>(earth.GetHeight()),
                Gdiplus::UnitPixel, &attributes) == Gdiplus::Ok;
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            constexpr float height = 198, top = 62, width = height * EmbraceMark::aspect;
            Gdiplus::GraphicsPath hairPath(Gdiplus::FillModeWinding);
            const EmbraceMark::Face* hair = nullptr;
            for (const auto& face : EmbraceMark::faces) {
                if (!includeFigure) break;
                Gdiplus::PointF points[64];
                if (face.count > 64) { okay = false; break; }
                for (unsigned i = 0; i < face.count; ++i) {
                    const auto& vertex = EmbraceMark::vertices[face.start + i];
                    points[i] = {(size.cx - width) / 2 + vertex.x * width, top + vertex.y * height};
                }
                if (face.hair) { hair = &face; hairPath.AddPolygon(points, face.count); continue; }
                // Flatten the faded figure against the theme, so Earth textures cannot show through it.
                if (FillEmbraceFace(graphics, face, points, top, height, surface, .22f) != Gdiplus::Ok)
                    okay = false;
            }
            if (hair && FillEmbraceFace(graphics, *hair, nullptr, top, height, surface, .22f, &hairPath) != Gdiplus::Ok)
                okay = false;
        }
    }
    graphics.Restore(state);
    stream->Release();
    return okay;
}

// Lucy's art is screen-blended (black stays clear, the hair glows), then a left-to-right scrim
// in the surface colour keeps the candidate column readable. Positions are fixed from the top,
// so shorter pages only crop the bottom.
void PaintLucyArt(Gdiplus::Bitmap& canvas, Gdiplus::Bitmap& photo, SIZE size, COLORREF surface, bool flowing) {
    Gdiplus::Bitmap art(size.cx, size.cy, PixelFormat32bppARGB);
    if (art.GetLastStatus() != Gdiplus::Ok) return;
    {
        Gdiplus::Graphics layer(&art);
        layer.Clear(Gdiplus::Color(0, 0, 0, 0));
        layer.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        layer.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        const Gdiplus::RectF place = flowing ? Gdiplus::RectF(74, 62, 200, 167) : Gdiplus::RectF(-192, -30, 704, 396);
        layer.DrawImage(&photo, place, 0, 0, static_cast<float>(photo.GetWidth()),
            static_cast<float>(photo.GetHeight()), Gdiplus::UnitPixel);
    }
    const float opacity = flowing ? .70f : .85f;
    constexpr float scrimAt[] = {0, .44f, .66f, .88f, 1}, scrimAlpha[] = {.96f, .92f, .55f, .08f, .08f};
    std::vector<float> scrim(size.cx);
    for (int x = 0; x < size.cx; ++x) {
        const float t = (x + .5f) / size.cx;
        int i = 0;
        while (i < 3 && t > scrimAt[i + 1]) ++i;
        const float f = (std::clamp)((t - scrimAt[i]) / (scrimAt[i + 1] - scrimAt[i]), 0.0f, 1.0f);
        scrim[x] = scrimAlpha[i] + (scrimAlpha[i + 1] - scrimAlpha[i]) * f;
    }
    Gdiplus::Rect all(0, 0, size.cx, size.cy);
    Gdiplus::BitmapData source = {}, target = {};
    if (art.LockBits(&all, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &source) != Gdiplus::Ok) return;
    if (canvas.LockBits(&all, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
        PixelFormat32bppARGB, &target) != Gdiplus::Ok) {
        art.UnlockBits(&source);
        return;
    }
    const float base[] = {static_cast<float>(GetBValue(surface)), static_cast<float>(GetGValue(surface)),
        static_cast<float>(GetRValue(surface))};
    for (int y = 0; y < size.cy; ++y) {
        const BYTE* from = static_cast<const BYTE*>(source.Scan0) + y * source.Stride;
        BYTE* to = static_cast<BYTE*>(target.Scan0) + y * target.Stride;
        for (int x = 0; x < size.cx; ++x, from += 4, to += 4) {
            const float alpha = from[3] / 255.0f * opacity;
            for (int c = 0; c < 3; ++c) {
                const float under = to[c];
                const float screen = 255 - (255 - from[c]) * (255 - under) / 255;
                const float lit = under + (screen - under) * alpha;
                to[c] = static_cast<BYTE>(lit + (base[c] - lit) * scrim[x] + .5f);
            }
            to[3] = 255;
        }
    }
    canvas.UnlockBits(&target);
    art.UnlockBits(&source);
}

HBITMAP MakeBackground(HINSTANCE instance, SIZE size, COLORREF surface, CandidateTheme theme) {
    const WORD resourceId = theme == CandidateTheme::Lucy ? 305 : theme == CandidateTheme::LucyFlow ? 306 : 303;
    IStream* stream = EmbeddedImageStream(instance, resourceId);
    if (!stream) return nullptr;
    Gdiplus::GdiplusStartupInput startup;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) {
        stream->Release();
        return nullptr;
    }
    HBITMAP result = nullptr;
    {
        // Decode and blend once per theme/size, outside DllMain. Repaints only BitBlt.
        Gdiplus::Bitmap photo(stream);
        Gdiplus::Bitmap canvas(size.cx, size.cy, PixelFormat32bppPARGB);
        if (photo.GetLastStatus() == Gdiplus::Ok && canvas.GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics graphics(&canvas);
            graphics.Clear(Gdiplus::Color(255, GetRValue(surface), GetGValue(surface), GetBValue(surface)));
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            Gdiplus::ColorMatrix matrix = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0},
                {0, 0, 0, 0.15f, 0}, {0, 0, 0, 0, 1}}};
            Gdiplus::ImageAttributes attributes;
            attributes.SetColorMatrix(&matrix);
            attributes.SetColorKey(Gdiplus::Color(255, 0, 0, 0), Gdiplus::Color(255, 18, 18, 18));
            if (IsLucyTheme(theme)) {
                graphics.Flush(Gdiplus::FlushIntentionSync);
                PaintLucyArt(canvas, photo, size, surface, theme == CandidateTheme::LucyFlow);
            } else {
            const float scaleX = photo.GetWidth() / 1280.0f, scaleY = photo.GetHeight() / 1280.0f;
            // Keep the original scene position; shorter panels only crop its bottom.
            constexpr int diameter = 148, top = 54;
            if (!IsLargeEarthTheme(theme))
                graphics.DrawImage(&photo, Gdiplus::Rect((size.cx - diameter) / 2, top, diameter, diameter),
                    600 * scaleX, 540 * scaleY, 250 * scaleX, 250 * scaleY, Gdiplus::UnitPixel, &attributes);
            const int horizon = (std::max)(40L, size.cy / 5);
            graphics.DrawImage(&photo, Gdiplus::Rect(0, size.cy - horizon, size.cx, horizon),
                0.0f, 925 * scaleY, static_cast<float>(photo.GetWidth()), 355 * scaleY, Gdiplus::UnitPixel, &attributes);
            if (IsLargeEarthTheme(theme))
                DrawLargeEarthBackground(graphics, instance, size, surface, theme == CandidateTheme::DavidLucyEmbrace);
            }
            graphics.Flush(Gdiplus::FlushIntentionSync);
            canvas.GetHBITMAP(Gdiplus::Color(255, 0, 0, 0), &result);
        }
    }
    // Both the source bitmap and graphics must die before their stream/runtime.
    stream->Release();
    Gdiplus::GdiplusShutdown(token);
    return result;
}

const Palette& ThemePalette(CandidateTheme theme, bool dark) {
    switch (theme) {
    case CandidateTheme::Terminal: return kTerminal;
    case CandidateTheme::GameBoy: return kGameBoy;
    case CandidateTheme::Lucy:
    case CandidateTheme::LucyFlow: return kLucy;
    case CandidateTheme::DavidLucy: return kDavidLucy;
    case CandidateTheme::DavidLucyEmbrace:
    case CandidateTheme::Earth: return kDavidLucyEmbrace;
    default: return dark ? kDark : kLight;
    }
}

struct CyberChrome { COLORREF panel, outer, edge, secondary; };
CyberChrome ThemeChrome(CandidateTheme theme) {
    switch (theme) {
    case CandidateTheme::Lucy:
    case CandidateTheme::LucyFlow: return {RGB(31, 28, 48), RGB(12, 10, 21), RGB(105, 82, 128), RGB(240, 169, 209)};
    case CandidateTheme::DavidLucy:
    case CandidateTheme::DavidLucyEmbrace:
    case CandidateTheme::Earth: return {RGB(34, 24, 52), RGB(8, 9, 19), RGB(112, 70, 151), RGB(197, 112, 244)};
    default: return {RGB(29, 19, 48), RGB(8, 7, 17), RGB(112, 69, 143), RGB(255, 105, 194)};
    }
}

bool AppsUseDarkMode() {
    HKEY key = nullptr;
    constexpr wchar_t path[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD value = 1, type = 0, size = sizeof(value);
    LONG result = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
        reinterpret_cast<BYTE*>(&value), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(value) && value == 0;
}

void Fill(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void Panel(HDC dc, RECT rect, COLORREF fill, COLORREF edge, int radius = 0) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
    if (radius) RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    else Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
    DeleteObject(brush); DeleteObject(pen);
}

void Shape(HDC dc, const POINT* points, int count, COLORREF fill, COLORREF edge) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
    Polygon(dc, points, count);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
    DeleteObject(brush); DeleteObject(pen);
}

void Disc(HDC dc, int x, int y, int radius, COLORREF fill, COLORREF edge) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
    Ellipse(dc, x - radius, y - radius, x + radius + 1, y + radius + 1);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
    DeleteObject(brush); DeleteObject(pen);
}

void EarthMoonRoute(HDC dc, int width, int footerY, int footerHeight, int page, int pages, bool loading) {
    const int earth = 28, moon = width - 28, y = footerY + footerHeight / 2;
    if (loading) {
        Disc(dc, earth, y, 9, kDavidLucy.surface, kDavidLucy.muted);
        Disc(dc, moon, y, 6, kDavidLucy.surface, kDavidLucy.muted);
        return;
    }
    // The shared journey stays below the text, with page progress as its route.
    Disc(dc, earth, y, 9, RGB(86, 49, 120), RGB(149, 243, 184));
    Disc(dc, earth + 1, y, 7, RGB(29, 105, 135), RGB(84, 184, 194));
    const POINT north[] = {{earth - 4, y - 5}, {earth, y - 6}, {earth + 1, y - 3},
        {earth - 1, y}, {earth - 4, y - 1}};
    const POINT south[] = {{earth + 2, y + 1}, {earth + 5, y}, {earth + 6, y + 3},
        {earth + 3, y + 6}, {earth + 1, y + 4}};
    Shape(dc, north, 5, RGB(149, 243, 184), RGB(149, 243, 184));
    Shape(dc, south, 5, RGB(94, 202, 164), RGB(94, 202, 164));
    Disc(dc, moon, y, 6, RGB(217, 213, 241), RGB(197, 112, 244));
    Disc(dc, moon - 2, y - 2, 2, RGB(163, 147, 192), RGB(163, 147, 192));
    Disc(dc, moon + 2, y + 2, 1, RGB(163, 147, 192), RGB(163, 147, 192));
    const int start = earth + 12, end = moon - 12;
    for (int x = start; x <= end; x += 4)
        Fill(dc, {x, y + 11, x + 2, y + 12}, kDavidLucy.line);
    const int position = start + (page - 1) * (end - start) / (std::max)(1, pages - 1);
    Fill(dc, {position - 1, y + 9, position + 2, y + 12}, kDavidLucy.accent);
}

void Stroke(HDC dc, const POINT* points, int count, COLORREF color, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ old = SelectObject(dc, pen);
    Polyline(dc, points, count);
    SelectObject(dc, old); DeleteObject(pen);
}

void LucyHair(HDC dc, int x, int y) {
    // Three flat shapes, matching the editable VectorCraft mark.
    HBRUSH brush = CreateSolidBrush(kLucy.ink);
    HGDIOBJ old = SelectObject(dc, brush);
    BeginPath(dc);
    MoveToEx(dc, x - 11, y, nullptr);
    LineTo(dc, x - 11, y - 3);
    const POINT crown[] = {{x - 11, y - 9}, {x - 6, y - 14}, {x, y - 14},
        {x + 6, y - 14}, {x + 11, y - 9}, {x + 11, y - 3}};
    PolyBezierTo(dc, crown, 6);
    LineTo(dc, x + 11, y);
    CloseFigure(dc); EndPath(dc); FillPath(dc);
    SelectObject(dc, old); DeleteObject(brush);
    Fill(dc, {x, y - 3, x + 11, y + 5}, kLucy.selected);
    const POINT strand[] = {{x - 11, y - 3}, {x - 6, y - 3},
        {x - 6, y + 9}, {x - 11, y + 14}};
    Shape(dc, strand, 4, kLucy.accent, kLucy.accent);
}
template <typename Paint>
bool PaintVectorMark(HDC dc, Paint paint) {
    Gdiplus::GdiplusStartupInput startup;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) return false;
    bool okay = true;
    XFORM transform = {1, 0, 0, 1, 0, 0};
    GetWorldTransform(dc, &transform);
    const int state = SaveDC(dc);
    const XFORM identity = {1, 0, 0, 1, 0, 0};
    SetWorldTransform(dc, &identity);
    {
        Gdiplus::Graphics graphics(dc);
        // The cached GDI surface uses a 3x world transform; preserve subpixel vertices.
        Gdiplus::Matrix matrix(transform.eM11, transform.eM12, transform.eM21,
            transform.eM22, transform.eDx, transform.eDy);
        graphics.SetTransform(&matrix);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        okay = paint(graphics);
    }
    RestoreDC(dc, state);
    Gdiplus::GdiplusShutdown(token);
    return okay;
}

bool DrawEmbraceMark(HDC dc, int x, int y) {
    return PaintVectorMark(dc, [x, y](Gdiplus::Graphics& graphics) {
        bool okay = true;
        // Keep the head at the top; the cache crops the enlarged lower body.
        constexpr float height = 48, width = height * EmbraceMark::aspect;
        Gdiplus::GraphicsPath hairPath(Gdiplus::FillModeWinding);
        const EmbraceMark::Face* hair = nullptr;
        for (const auto& face : EmbraceMark::faces) {
            Gdiplus::PointF points[64];
            if (face.count > 64) { okay = false; break; }
            for (unsigned i = 0; i < face.count; ++i) {
                const auto& vertex = EmbraceMark::vertices[face.start + i];
                points[i] = {x - width / 2 + vertex.x * width, y - 17 + vertex.y * height};
            }
            if (face.hair) { hair = &face; hairPath.AddPolygon(points, face.count); continue; }
            if (FillEmbraceFace(graphics, face, points, static_cast<float>(y - 17), height, 0, 1) != Gdiplus::Ok)
                okay = false;
        }
        if (hair && FillEmbraceFace(graphics, *hair, nullptr, static_cast<float>(y - 17), height, 0, 1, &hairPath) != Gdiplus::Ok)
            okay = false;
        return okay;
    });
}

bool DrawLucyMark(HDC dc, int x, int y) {
    return PaintVectorMark(dc, [x, y](Gdiplus::Graphics& graphics) {
        auto color = [](unsigned rgb) { return Gdiplus::Color(255, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); };
        constexpr float scale = 1.1f;
        const float left = x - LucyMark::width * scale / 2, top = y - LucyMark::height * scale / 2;
        auto at = [&](const LucyMark::Point& p) { return Gdiplus::PointF(left + p.x * scale, top + p.y * scale); };
        bool okay = true;
        for (const auto& shape : LucyMark::shapes) {
            Gdiplus::PointF points[160];
            if (shape.count > 160) return false;
            for (unsigned i = 0; i < shape.count; ++i) points[i] = at(LucyMark::points[shape.start + i]);
            Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);
            path.AddBeziers(points, shape.count);
            path.CloseFigure();
            if (shape.gradient < 0) {
                Gdiplus::SolidBrush brush(color(shape.color));
                okay = graphics.FillPath(&brush, &path) == Gdiplus::Ok && okay;
                continue;
            }
            // Stops are baked to span the whole shape, matching the SVG's padded gradients.
            const auto& gradient = LucyMark::gradients[shape.gradient];
            Gdiplus::Color colors[8];
            Gdiplus::REAL offsets[8];
            if (gradient.count > 8) return false;
            for (unsigned i = 0; i < gradient.count; ++i) {
                colors[i] = color(LucyMark::stops[gradient.start + i].color);
                offsets[i] = LucyMark::stops[gradient.start + i].offset;
            }
            Gdiplus::LinearGradientBrush brush(at(gradient.from), at(gradient.to), colors[0], colors[gradient.count - 1]);
            brush.SetInterpolationColors(colors, offsets, gradient.count);
            okay = graphics.FillPath(&brush, &path) == Gdiplus::Ok && okay;
        }
        return okay;
    });
}

void ThemeMark(HDC dc, CandidateTheme theme, RECT area, const Palette& colors) {
    const int x = (area.left + area.right) / 2, y = (area.top + area.bottom) / 2;
    switch (theme) {
    case CandidateTheme::Terminal: {
        Panel(dc, {x - 14, y - 9, x + 14, y + 10}, colors.surface, colors.muted);
        const POINT prompt[] = {{x - 8, y - 4}, {x - 3, y}, {x - 8, y + 4}};
        Stroke(dc, prompt, 3, colors.ink);
        Fill(dc, {x + 1, y + 4, x + 8, y + 5}, colors.ink);
        break;
    }
    case CandidateTheme::Lucy:
    case CandidateTheme::LucyFlow: {
        if (!DrawLucyMark(dc, x, y)) LucyHair(dc, x, y);
        break;
    }
    case CandidateTheme::DavidLucy:
    case CandidateTheme::DavidLucyEmbrace:
    case CandidateTheme::Earth: {
        if (!DrawEmbraceMark(dc, x, y)) LucyHair(dc, x, y);
        break;
    }
    default: {
        DrawTextW(dc, L"粵拼", -1, &area, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    }
}

void ThemeFrame(HDC dc, CandidateTheme theme, SIZE size) {
    const int w = size.cx, h = size.cy;
    if (theme == CandidateTheme::GameBoy) {
        // Keep the DMG bezel, not the controller body: this is a typing popup.
        Panel(dc, {0, 0, w, h}, RGB(197, 197, 190), RGB(129, 130, 124), 8);
        Panel(dc, {7, 9, w - 7, h - 5}, RGB(92, 94, 101), RGB(72, 74, 81), 5);
        Fill(dc, {15, 4, 41, 6}, RGB(135, 45, 87));
        Fill(dc, {44, 4, 70, 6}, RGB(40, 48, 99));
        Fill(dc, {w - 20, 4, w - 16, 7}, RGB(190, 38, 48));
    } else if (HasEdgerunnerPanel(theme)) {
        const Palette& colors = ThemePalette(theme, false);
        const CyberChrome chrome = ThemeChrome(theme);
        // A bevelled bezel on all four corners (12 px, larger than the other cyber themes), with the side ticks.
        Fill(dc, {0, 0, w, h}, chrome.outer);
        const POINT bezel[] = {{12, 0}, {w - 13, 0}, {w - 1, 12}, {w - 1, h - 13},
            {w - 13, h - 1}, {12, h - 1}, {0, h - 13}, {0, 12}};
        Shape(dc, bezel, 8, chrome.panel, chrome.edge);
        for (int y = 14; y < h - 12; y += 6) {
            Fill(dc, {1, y, 3, y + 2}, colors.line);
            Fill(dc, {w - 3, y, w - 1, y + 2}, colors.line);
        }
        const bool lucy = IsLucyTheme(theme);
        Fill(dc, {18, 0, 82, 2}, lucy ? RGB(186, 95, 242) : chrome.secondary);
        Fill(dc, {w - 82, h - 2, w - 18, h}, lucy ? kLucyPicked : colors.accent);
    } else if (IsCyberTheme(theme)) {
        const Palette& colors = ThemePalette(theme, false);
        const CyberChrome chrome = ThemeChrome(theme);
        Fill(dc, {0, 0, w, h}, chrome.outer);
        const POINT bezel[] = {{8, 0}, {w - 8, 0}, {w - 1, 7}, {w - 1, h - 8},
            {w - 8, h - 1}, {8, h - 1}, {0, h - 8}, {0, 8}};
        Shape(dc, bezel, 8, chrome.panel, chrome.edge);
        Fill(dc, {10, 1, 62, 3}, colors.accent);
        Fill(dc, {w - 62, 1, w - 10, 3}, chrome.secondary);
        for (int y = 14; y < h - 12; y += 6) {
            Fill(dc, {1, y, 3, y + 2}, colors.line);
            Fill(dc, {w - 3, y, w - 1, y + 2}, colors.line);
        }
        Fill(dc, {10, h - 3, 38, h - 1}, chrome.secondary);
        Fill(dc, {w - 38, h - 3, w - 10, h - 1}, colors.accent);
        if (IsDavidLucyTheme(theme))
            Fill(dc, {w / 2 - 12, 1, w / 2 + 12, 3}, RGB(211, 211, 239));
    }
}
} // namespace

CandidateWindow::Layout CandidateWindow::GetLayout() const {
    const int rows = (std::max)(1, static_cast<int>(m_candidates.size()));
    if (m_theme != CandidateTheme::System) {
        const int height = 36 + rows * 32 + 28;
        if (m_theme == CandidateTheme::GameBoy)
            return {{256, height + 20}, {10, 12, 246, height + 12}, 36, 32, 28};
        if (IsCyberTheme(m_theme))
            return {{256, height + 8}, {4, 4, 252, height + 4}, 36, 32, 28};
        return {{248, height}, {0, 0, 248, height}, 36, 32, 28};
    }
    const int height = HEADER_H + ITEM_H * rows + FOOTER_H;
    return {{WIN_W, height}, {0, 0, WIN_W, height}, HEADER_H, ITEM_H, FOOTER_H};
}

CandidateWindow::CandidateWindow() {}
CandidateWindow::~CandidateWindow() {
    if (m_hwnd) DestroyWindow(m_hwnd);
    for (HFONT font : m_fonts) if (font) DeleteObject(font);
    if (m_letterResource) RemoveFontMemResourceEx(m_letterResource);
    if (m_background) DeleteObject(m_background);
    if (m_mark) DeleteObject(m_mark);
    if (m_gradientTitle) DeleteObject(m_gradientTitle);
    if (m_plate) DeleteObject(m_plate);
}

// The highlighted row: a chamfered plate. Lucy's uses her hair gradient and fades left to right;
// David & Lucy's is solid and opaque with a sharp purple afterimage offset down-left.
// Built once per theme, then one AlphaBlend per paint.
void CandidateWindow::DrawSelectionPlate(HDC dc, int left, int top, int width, int height, COLORREF solid) {
    if (width <= 0 || height <= 0) return;
    const bool ghost = IsDavidLucyTheme(m_theme);
    constexpr int offset = 3, cut = 6;  // equal right and bottom offsets
    const int pad = ghost ? offset : 0;
    const int fullWidth = width + pad, fullHeight = height + pad;
    if (!m_plate || m_plateTheme != m_theme || m_plateSize.cx != width || m_plateSize.cy != height) {
        if (m_plate) DeleteObject(m_plate);
        m_plate = nullptr;
        BITMAPINFO info = {};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), fullWidth, -fullHeight, 1, 32, BI_RGB};
        void* data = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        if (!bitmap || !data) { if (bitmap) DeleteObject(bitmap); return; }
        auto* pixels = static_cast<std::uint32_t*>(data);
        auto premultiply = [](COLORREF color, unsigned alpha) {
            return (alpha << 24) | (((GetRValue(color) * alpha + 127) / 255) << 16) |
                (((GetGValue(color) * alpha + 127) / 255) << 8) | ((GetBValue(color) * alpha + 127) / 255);
        };
        // Same chamfers as the cyber card: top-right and bottom-left.
        auto inside = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < width && y < height && !(x - (width - cut) > y || y - (height - cut) > x);
        };
        const std::uint32_t shadow = premultiply(ThemeChrome(m_theme).secondary, 255);
        for (int y = 0; y < fullHeight; ++y) {
            for (int x = 0; x < fullWidth; ++x) {
                std::uint32_t pixel = 0;
                if (inside(x - pad, y)) {
                    const float t = (x - pad + .5f) / width;
                    pixel = IsLucyTheme(m_theme) ? premultiply(GradientColor(kLucyHair, kLucyPlateStops, 5, t),
                        static_cast<unsigned>((kPlateAlphaLeft + (kPlateAlphaRight - kPlateAlphaLeft) * t) * 255 + .5f)) :
                        premultiply(solid, 255);
                } else if (ghost && inside(x, y - pad)) {
                    pixel = shadow;
                }
                pixels[y * fullWidth + x] = pixel;
            }
        }
        m_plate = bitmap; m_plateSize = {width, height}; m_plateTheme = m_theme;
    }
    HDC memory = CreateCompatibleDC(dc);
    if (!memory) return;
    HGDIOBJ old = SelectObject(memory, m_plate);
    const BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, left - pad, top, fullWidth, fullHeight, memory, 0, 0, fullWidth, fullHeight, blend);
    SelectObject(memory, old); DeleteDC(memory);
}

void CandidateWindow::CacheLetterMetrics(HDC dc) {
    auto& data = m_letters;
    data = {};
    HGDIOBJ previous = SelectObject(dc, m_fonts[4]);
    GetTextMetricsW(dc, &data.text);
    GetCharABCWidthsFloatW(dc, L'A', L'Z', data.widths);
    SIZE space = {};
    GetTextExtentPoint32W(dc, L" ", 1, &space);
    data.space = static_cast<float>(space.cx);
    std::vector<KERNINGPAIR> pairs(GetKerningPairsW(dc, 0, nullptr));
    if (!pairs.empty()) GetKerningPairsW(dc, static_cast<DWORD>(pairs.size()), pairs.data());
    for (const auto& pair : pairs) {
        int a = LetterIndex(pair.wFirst), b = LetterIndex(pair.wSecond);
        if (a >= 0 && b >= 0) data.kern[a][b] = pair.iKernAmount;
    }
    SelectObject(dc, previous);
}

int CandidateWindow::DrawGradientLetters(HDC dc, const std::wstring& text, RECT area) {
    const int width = area.right - area.left, height = area.bottom - area.top;
    if (width <= 0 || height <= 0) return 0;
    if (!m_gradientTitle || m_gradientTitleText != text || m_gradientTitleTheme != m_theme ||
        m_gradientTitleSize.cx != width || m_gradientTitleSize.cy != height) {
        if (m_gradientTitle) DeleteObject(m_gradientTitle);
        m_gradientTitle = nullptr;
        BITMAPINFO info = {};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
        void* data = nullptr;
        HDC mask = CreateCompatibleDC(dc);
        if (!mask) return DrawLetters(dc, text, area, true, false);
        HBITMAP bitmap = CreateDIBSection(mask, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        if (!bitmap || !data) {
            if (bitmap) DeleteObject(bitmap);
            DeleteDC(mask);
            return DrawLetters(dc, text, area, true, false);
        }
        auto* pixels = static_cast<std::uint32_t*>(data);
        std::fill_n(pixels, width * height, 0u);
        HGDIOBJ oldBitmap = SelectObject(mask, bitmap);
        HGDIOBJ oldFont = SelectObject(mask, GetCurrentObject(dc, OBJ_FONT));
        SetBkMode(mask, TRANSPARENT); SetTextColor(mask, RGB(255, 255, 255));
        m_gradientTitleAdvance = DrawLetters(mask, text, {0, 0, width, height}, true, false);
        GdiFlush();
        int inkLeft = width, inkRight = -1;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
            if (pixels[y * width + x] & 0xffffffu) {
                inkLeft = (std::min)(inkLeft, x); inkRight = (std::max)(inkRight, x);
            }
        std::vector<COLORREF> gradient(width);
        for (int x = 0; x < width; ++x)
            gradient[x] = LucyTitleGradientColor(static_cast<float>(x - inkLeft - 1) / (std::max)(1, inkRight - inkLeft - 2));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const COLORREF color = gradient[x];
                auto& pixel = pixels[y * width + x];
                const unsigned alpha = (std::max)({pixel & 255, (pixel >> 8) & 255, (pixel >> 16) & 255});
                pixel = (alpha << 24) | (((GetRValue(color) * alpha + 127) / 255) << 16) |
                    (((GetGValue(color) * alpha + 127) / 255) << 8) | ((GetBValue(color) * alpha + 127) / 255);
            }
        }
        SelectObject(mask, oldFont); SelectObject(mask, oldBitmap); DeleteDC(mask);
        m_gradientTitle = bitmap; m_gradientTitleSize = {width, height};
        m_gradientTitleText = text; m_gradientTitleTheme = m_theme;
    }
    HDC memory = CreateCompatibleDC(dc);
    if (!memory) return DrawLetters(dc, text, area, true, false);
    HGDIOBJ old = SelectObject(memory, m_gradientTitle);
    const BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    const BOOL painted = AlphaBlend(dc, area.left, area.top, width, height, memory, 0, 0, width, height, blend);
    SelectObject(memory, old); DeleteDC(memory);
    return painted ? m_gradientTitleAdvance : DrawLetters(dc, text, area, true, false);
}

int CandidateWindow::DrawLetters(HDC dc, const std::wstring& text, RECT area, bool paint, bool gradient) {
    if (paint && gradient && IsLucyTheme(m_theme) &&
        std::any_of(text.begin(), text.end(), [](wchar_t ch) { return LetterIndex(ch) >= 0; }))
        return DrawGradientLetters(dc, text, area);
    const HFONT fallback = static_cast<HFONT>(GetCurrentObject(dc, OBJ_FONT));
    if (!IsCyberTheme(m_theme) || !m_fonts[4] ||
        !std::any_of(text.begin(), text.end(), [](wchar_t ch) { return LetterIndex(ch) >= 0; })) {
        if (paint) DrawTextW(dc, text.c_str(), -1, &area, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SIZE size = {};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
        return size.cx;
    }
    const auto& data = m_letters;
    struct Cell { size_t offset; int count; float x, end; HFONT font; };
    std::vector<Cell> cells;
    cells.reserve(text.size());
    float x = 0;
    int previousLetter = -1;
    for (size_t i = 0; i < text.size(); ++i) {
        const int letter = LetterIndex(text[i]);
        int count = 1;
        float advance;
        HFONT font = fallback;
        if (letter >= 0) {
            if (previousLetter >= 0) x += data.kern[previousLetter][letter];
            const auto& abc = data.widths[letter];
            advance = abc.abcfA + abc.abcfB + abc.abcfC;
            font = m_fonts[4];
        } else if (text[i] == L' ') {
            advance = data.space;
        } else {
            // Keep digits, punctuation and CJK in the existing theme font.
            if (text[i] >= 0xD800 && text[i] <= 0xDBFF && i + 1 < text.size() &&
                text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) count = 2;
            SIZE size = {};
            GetTextExtentPoint32W(dc, text.c_str() + i, count, &size);
            advance = static_cast<float>(size.cx);
        }
        cells.push_back({i, count, x, x + advance, font});
        x += advance;
        previousLetter = letter;
        i += count - 1;
    }
    if (!paint) return static_cast<int>(std::ceil(x));
    const int saved = SaveDC(dc);
    if (!saved) return static_cast<int>(std::ceil(x));
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    SetTextAlign(dc, TA_LEFT | TA_BASELINE | TA_NOUPDATECP);
    const int baseline = area.top + (area.bottom - area.top - data.text.tmHeight) / 2 + data.text.tmAscent;
    const bool truncate = x > area.right - area.left;
    SIZE dots = {};
    GetTextExtentPoint32W(dc, L"…", 1, &dots);
    const int limit = area.right - area.left - (truncate ? dots.cx : 0);
    for (const auto& cell : cells) {
        if (cell.end > limit) break;
        SelectObject(dc, cell.font);
        TextOutW(dc, area.left + static_cast<int>(std::lround(cell.x)), baseline,
            text.c_str() + cell.offset, cell.count);
    }
    if (truncate) {
        SelectObject(dc, fallback);
        TextOutW(dc, area.right - dots.cx, baseline, L"…", 1);
    }
    RestoreDC(dc, saved);
    return static_cast<int>(std::ceil(x));
}

void CandidateWindow::DrawThemeMark(HDC dc, RECT area) {
    if (m_theme == CandidateTheme::System || m_theme == CandidateTheme::GameBoy) {
        DrawTextW(dc, L"粵拼", -1, &area, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    constexpr int scale = 3;
    const int width = HasWideMark(m_theme) ? 44 : 36;
    const int height = HasWideMark(m_theme) ? 34 : 32;
    const int left = (area.left + area.right - width) / 2;
    const int top = IsDavidLucyTheme(m_theme) ? 0 : (area.top + area.bottom - height) / 2;
    if (!m_mark || m_markTheme != m_theme) {
        if (m_mark) DeleteObject(m_mark);
        m_mark = CreateCompatibleBitmap(dc, width * scale, height * scale);
        m_markTheme = m_theme;
        if (!m_mark) return;
        HDC memory = CreateCompatibleDC(dc);
        HGDIOBJ previous = SelectObject(memory, m_mark);
        SetGraphicsMode(memory, GM_ADVANCED);
        const XFORM transform = {scale, 0, 0, scale, 0, 0};
        SetWorldTransform(memory, &transform);
        const Palette& colors = ThemePalette(m_theme, false);
        Fill(memory, {0, 0, width, height}, IsCyberTheme(m_theme) && !HasMoonBackground(m_theme) ? ThemeChrome(m_theme).panel : colors.surface);
        if (IsLargeEarthTheme(m_theme) && m_background) {
            // Bake the matching photograph into the icon cache to avoid an opaque rectangle in the header.
            HDC background = CreateCompatibleDC(dc);
            HGDIOBJ oldBackground = SelectObject(background, m_background);
            StretchBlt(memory, 0, 0, width, height, background, left, top, width, height, SRCCOPY);
            SelectObject(background, oldBackground); DeleteDC(background);
        }
        ThemeMark(memory, m_theme, {0, 0, width, height}, colors);
        SelectObject(memory, previous); DeleteDC(memory);
    }
    HDC memory = CreateCompatibleDC(dc);
    HGDIOBJ previous = SelectObject(memory, m_mark);
    const int stretch = SetStretchBltMode(dc, HALFTONE);
    POINT brushOrigin; SetBrushOrgEx(dc, 0, 0, &brushOrigin);
    StretchBlt(dc, left, top,
        width, height, memory, 0, 0, width * scale, height * scale, SRCCOPY);
    SetBrushOrgEx(dc, brushOrigin.x, brushOrigin.y, nullptr);
    SetStretchBltMode(dc, stretch);
    SelectObject(memory, previous); DeleteDC(memory);
}

void CandidateWindow::DrawBackground(HDC dc, RECT bounds, COLORREF surface) {
    const SIZE size = {bounds.right - bounds.left, bounds.bottom - bounds.top};
    if (m_backgroundTheme != m_theme || m_backgroundSize.cx != size.cx || m_backgroundSize.cy != size.cy) {
        if (m_background) DeleteObject(m_background);
        m_background = MakeBackground(s_hInst, size, surface, m_theme);
        m_backgroundSize = size;
        m_backgroundTheme = m_theme;
    }
    if (!m_background) return;
    HDC memory = CreateCompatibleDC(dc);
    HGDIOBJ previous = SelectObject(memory, m_background);
    BitBlt(dc, bounds.left, bounds.top, size.cx, size.cy, memory, 0, 0, SRCCOPY);
    SelectObject(memory, previous);
    DeleteDC(memory);
}

bool CandidateWindow::RegisterClass(HINSTANCE hInst) {
    s_hInst = hInst;
    s_className = L"YutpingCandidateWnd_" +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(hInst));
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = s_className.c_str();
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void CandidateWindow::Show(const std::wstring& composition,
                           const std::vector<Candidate>& candidates, POINT pt,
                           int page, int totalPages, bool loading, bool cached, bool offline, int selectedRow) {
    m_composition = composition;
    // While a newer API request is in flight, keep the previous rows as a
    // disabled visual placeholder. This keeps the popup from collapsing and
    // expanding on every typed letter.
    if (!loading || !candidates.empty() || m_candidates.empty()) {
        m_candidates = candidates;
        m_page = page;
        m_totalPages = totalPages;
        m_selectedRow = (std::clamp)(selectedRow, 0, (std::max)(0, static_cast<int>(candidates.size()) - 1));
    }
    m_loading = loading;
    m_cached = cached;
    m_offline = offline;
    m_hoverRow = -1;

    if (!m_hwnd) {
        m_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            s_className.c_str(), L"", WS_POPUP,
            pt.x, pt.y, WIN_W, HEADER_H + ITEM_H + FOOTER_H,
            nullptr, nullptr, s_hInst, this);
        if (!m_hwnd) return;
    }

    const SIZE size = PreferredSize();
    const int width = size.cx, height = size.cy;
    MONITORINFO monitor = { sizeof(monitor) };
    if (!GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    const RECT& work = monitor.rcWork;
    int x = (std::max)(static_cast<int>(work.left),
        (std::min)(static_cast<int>(pt.x), static_cast<int>(work.right) - width));
    int y = pt.y + 4;
    if (y + height > work.bottom) y = pt.y - height - 4;
    y = (std::max)(static_cast<int>(work.top), y);

    if (m_shapeSize.cx != width || m_shapeSize.cy != height || m_shapeTheme != m_theme) {
        // The system owns the region after SetWindowRgn; nullptr restores the rectangle.
        SetWindowRgn(m_hwnd, BevelRegion(m_theme, size), FALSE);
        m_shapeSize = size;
        m_shapeTheme = m_theme;
    }
    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, width, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CandidateWindow::Hide() {
    if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
    m_candidates.clear();
    m_composition.clear();
    m_loading = false;
    m_selectionPending = false;
    m_cached = m_offline = false;
    m_hoverRow = -1;
    m_page = m_totalPages = 1;
    m_selectedRow = 0;
}

// Cut the window along the bezel's diagonal edge line so the bevelled corners are truly open.
// Vertices are chosen for GDI's region rasterisation so every edge pixel stays inside
// and every pixel beyond it is cut (the bottom corners need one pixel more inset).
HRGN CandidateWindow::BevelRegion(CandidateTheme theme, SIZE size) {
    if (!HasEdgerunnerPanel(theme)) return nullptr;
    const int w = size.cx, h = size.cy;
    const POINT outline[] = {{12, 0}, {w - 12, 0}, {w, 12}, {w, h - 13},
        {w - 13, h}, {13, h}, {0, h - 13}, {0, 12}};
    return CreatePolygonRgn(outline, 8, WINDING);
}

bool CandidateWindow::IsVisible() const {
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void CandidateWindow::Paint(HWND hwnd) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    RECT bounds;
    GetClientRect(hwnd, &bounds);
    Draw(dc, bounds, m_theme == CandidateTheme::System && AppsUseDarkMode());
    EndPaint(hwnd, &paint);
}

void CandidateWindow::Draw(HDC dc, RECT bounds, bool dark) {
    const Layout layout = GetLayout();
    const int WIN_W = layout.content.right - layout.content.left;
    const int HEADER_H = layout.header, ITEM_H = layout.row, FOOTER_H = layout.footer;
    const Palette& colors = ThemePalette(m_theme, dark);
    const bool cyber = IsCyberTheme(m_theme);
    const CyberChrome chrome = ThemeChrome(m_theme);
    const bool mono = m_theme == CandidateTheme::Terminal || m_theme == CandidateTheme::GameBoy || cyber;
    const wchar_t* latinFont = mono ? L"Consolas" : L"Segoe UI";
    const DWORD quality = m_theme == CandidateTheme::GameBoy ? NONANTIALIASED_QUALITY :
        m_theme == CandidateTheme::Terminal ? ANTIALIASED_QUALITY : CLEARTYPE_QUALITY;
    const DWORD latinQuality = cyber ? NONANTIALIASED_QUALITY : quality;
    Fill(dc, bounds, colors.surface);
    SetBkMode(dc, TRANSPARENT);

    if (cyber && !m_letterResourceAttempted) {
        m_letterResourceAttempted = true;
        HRSRC resource = FindResourceW(s_hInst, MAKEINTRESOURCEW(304), RT_RCDATA);
        const DWORD bytes = resource ? SizeofResource(s_hInst, resource) : 0;
        void* data = bytes ? LockResource(LoadResource(s_hInst, resource)) : nullptr;
        DWORD count = 0;
        if (data) m_letterResource = AddFontMemResourceEx(data, bytes, nullptr, &count);
    }

    if (!m_fonts[0] || m_fontTheme != m_theme || m_fontRowHeight != ITEM_H) {
        for (HFONT& font : m_fonts) { if (font) DeleteObject(font); font = nullptr; }
        m_fontTheme = m_theme;
        m_fontRowHeight = ITEM_H;
        m_fonts[0] = CreateFontW(mono ? -18 : -19, 0, 0, 0, cyber ? FW_BOLD : FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            latinQuality, DEFAULT_PITCH | FF_DONTCARE, latinFont);
        if (cyber && m_letterResource) {
            m_fonts[4] = CreateFontW(-26, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                DEFAULT_PITCH, L"Yutping Cyber Reference Study");
            CacheLetterMetrics(dc);
        }
        m_fonts[1] = CreateFontW(-19,
            0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            quality, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei UI");
        m_fonts[2] = CreateFontW(m_theme == CandidateTheme::GameBoy ? -13 : -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            latinQuality, DEFAULT_PITCH | FF_DONTCARE, latinFont);
        m_fonts[3] = CreateFontW(m_theme == CandidateTheme::GameBoy ? -13 : -14, 0, 0, 0,
            cyber ? FW_BOLD : FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            latinQuality, DEFAULT_PITCH | FF_DONTCARE, latinFont);
    }
    HFONT titleFont = m_fonts[0], itemFont = m_fonts[1], smallFont = m_fonts[2];
    ThemeFrame(dc, m_theme, layout.size);
    POINT origin;
    GetViewportOrgEx(dc, &origin);
    SetViewportOrgEx(dc, origin.x + layout.content.left, origin.y + layout.content.top, nullptr);
    bounds = {0, 0, WIN_W, layout.content.bottom - layout.content.top};
    Fill(dc, bounds, colors.surface);
    if (HasMoonBackground(m_theme) || IsLucyTheme(m_theme)) DrawBackground(dc, bounds, colors.surface);
    else if (m_background) {
        DeleteObject(m_background); m_background = nullptr; m_backgroundSize = {};
    }
    HFONT original = static_cast<HFONT>(SelectObject(dc, titleFont));
    if (cyber) {
        // Lucy's header stays clear so her art and scrim continue behind the letters.
        if (!HasMoonBackground(m_theme) && !IsLucyTheme(m_theme)) Fill(dc, {1, 1, WIN_W - 1, HEADER_H - 1}, chrome.panel);
        // CRT scan lines only cross the empty gutters, never the text.
        for (int y = HEADER_H + 3; y < bounds.bottom - FOOTER_H; y += 4)
            Fill(dc, {1, y, 5, y + 1}, colors.line);
    }

    const bool iconFree = IsIconFreeTheme(m_theme);
    const bool edgerunner = HasEdgerunnerPanel(m_theme);
    if (iconFree && m_mark) { DeleteObject(m_mark); m_mark = nullptr; }
    const bool textMark = m_theme == CandidateTheme::System || m_theme == CandidateTheme::GameBoy;
    const int markRight = WIN_W - PADDING - (iconFree ? 0 : textMark ? 5 : HasWideMark(m_theme) ? 9 : 13);
    const int markWidth = iconFree ? 0 : HasWideMark(m_theme) ? 44 : 36;
    RECT mode = {markRight - markWidth, 2, markRight, HEADER_H - 1};
    const wchar_t* status = m_loading ? (m_selectionPending ? L"等待選字" : L"更新中") : m_cached ? L"離線快取" : L"離線";
    RECT statusBox = {};
    if (m_loading || m_offline) {
        HGDIOBJ previous = SelectObject(dc, smallFont);
        SIZE text = {};
        GetTextExtentPoint32W(dc, status, static_cast<int>(wcslen(status)), &text);
        SelectObject(dc, previous);
        statusBox = {WIN_W - PADDING - text.cx - 10, (HEADER_H - 20) / 2,
            WIN_W - PADDING, (HEADER_H + 20) / 2};
        mode = {statusBox.left + 5, statusBox.top + 1, statusBox.right - 5, statusBox.bottom - 1};
    }
    RECT header = { PADDING + 6, 2, mode.left - 6, HEADER_H - 1 };
    if (iconFree) header.right = WIN_W - PADDING;
    if (m_loading || m_offline) header.right = statusBox.left - 6;
    if (m_theme == CandidateTheme::Terminal) {
        Fill(dc, {1, 1, WIN_W - 1, 3}, colors.accent);
        RECT prompt = {12, 2, 26, HEADER_H - 1};
        SetTextColor(dc, colors.accent);
        DrawTextW(dc, L">", -1, &prompt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        header.left = 30;
    }
    SetTextColor(dc, cyber ? colors.accent : colors.ink);
    const int lettersWidth = DrawLetters(dc, m_composition, header);
    if (edgerunner) {
        const int caret = header.left + lettersWidth + 4;
        if (caret + 2 <= header.right)
            Fill(dc, {caret, 9, caret + 2, 26}, IsLucyTheme(m_theme) ? RGB(196, 199, 240) : colors.accent);
    }
    if (m_theme == CandidateTheme::Terminal) {
        SIZE extent = {};
        GetTextExtentPoint32W(dc, m_composition.c_str(), static_cast<int>(m_composition.size()), &extent);
        if (header.left + extent.cx + 9 < header.right)
            Fill(dc, {header.left + extent.cx + 2, 10, header.left + extent.cx + 9, 26}, colors.accent);
    }
    SelectObject(dc, smallFont);
    if (cyber && (m_loading || m_offline)) {
        const POINT tab[] = {{statusBox.left, statusBox.top}, {statusBox.right - 4, statusBox.top},
            {statusBox.right, statusBox.top + 4}, {statusBox.right, statusBox.bottom}, {statusBox.left, statusBox.bottom}};
        Shape(dc, tab, 5, colors.surface, colors.line);
    }
    SetTextColor(dc, colors.muted);
    if (m_loading || m_offline) {
        DrawTextW(dc, status, -1, &mode, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    } else if (!iconFree) DrawThemeMark(dc, mode);
    RECT line = { 1, HEADER_H - 1, WIN_W - 1, HEADER_H };
    Fill(dc, line, colors.line);

    SelectObject(dc, itemFont);
    if (m_candidates.empty()) {
        RECT empty = { PADDING + 6, HEADER_H, WIN_W - PADDING, HEADER_H + ITEM_H };
        SetTextColor(dc, colors.muted);
        DrawTextW(dc, m_loading ? L"正在搜尋候選字…" :
            m_offline ? L"網路無法連線 · Enter 輸出拼音" : L"沒有候選字 · Enter 輸出拼音",
            -1, &empty, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        for (int i = 0; i < static_cast<int>(m_candidates.size()); ++i) {
            int top = HEADER_H + i * ITEM_H;
            RECT row = { 1, top, WIN_W - 1, top + ITEM_H };
            const bool selected = !m_loading && i == m_selectedRow;
            const COLORREF ink = selected && colors.selectedInk != CLR_INVALID ? colors.selectedInk : m_loading ? colors.muted : colors.ink;
            const COLORREF noteInk = selected && colors.selectedMuted != CLR_INVALID ? colors.selectedMuted : colors.muted;
            if (cyber)
                row = {6, top + 3, WIN_W - 6, top + ITEM_H - 3};
            if (selected) { if (!cyber) Fill(dc, row, colors.selected); }
            else if (!m_loading && i == m_hoverRow) Fill(dc, row, colors.hover);
            if (m_theme == CandidateTheme::Terminal)
                Fill(dc, {35, top, 36, top + ITEM_H}, colors.line);
            if (cyber) {
                if (!selected) {
                    Panel(dc, {12, top + 6, 35, top + ITEM_H - 6}, colors.surface, colors.line);
                    Fill(dc, {43, top + ITEM_H - 2, WIN_W - 28, top + ITEM_H - 1}, colors.line);
                    Fill(dc, {WIN_W - 23, top + ITEM_H - 2, WIN_W - 18, top + ITEM_H - 1}, colors.line);
                    Fill(dc, {WIN_W - 14, top + ITEM_H - 2, WIN_W - 10, top + ITEM_H - 1}, colors.line);
                } else if (edgerunner) {
                    DrawSelectionPlate(dc, 6, top + 3, WIN_W - 12, ITEM_H - 6, colors.selected);
                    Panel(dc, {12, top + 6, 35, top + ITEM_H - 6}, colors.surface, colors.surface);
                } else {
                    const POINT card[] = {{6, top + 3}, {WIN_W - 12, top + 3}, {WIN_W - 6, top + 9},
                        {WIN_W - 6, top + ITEM_H - 3}, {12, top + ITEM_H - 3}, {6, top + ITEM_H - 9}};
                    Shape(dc, card, 6, colors.selected, colors.selected);
                    Panel(dc, {12, top + 6, 35, top + ITEM_H - 6}, colors.surface, colors.surface);
                    Fill(dc, {6, top + 2, WIN_W - 14, top + 3}, colors.selectedMuted);
                    Fill(dc, {WIN_W - 10, top + 10, WIN_W - 6, top + ITEM_H - 1}, chrome.secondary);
                    if (!iconFree) {
                        const POINT cursor[] = {{WIN_W - 20, top + 11}, {WIN_W - 15, top + 16}, {WIN_W - 20, top + 21}};
                        Shape(dc, cursor, 3, colors.selectedInk, colors.selectedInk);
                    }
                }
            }
            if (selected && m_theme == CandidateTheme::GameBoy)
                Panel(dc, {9, top + 7, 32, top + ITEM_H - 7}, colors.selected, colors.accent);
            if (selected && m_theme == CandidateTheme::System) {
                RECT accent = { 1, top + 5, 4, top + ITEM_H - 5 };
                Fill(dc, accent, colors.accent);
            }
            RECT number = { PADDING + 6, top, PADDING + 34, top + ITEM_H };
            SelectObject(dc, smallFont);
            SetTextColor(dc, m_loading ? colors.muted :
                (selected ? (IsLucyTheme(m_theme) ? kLucyPicked : colors.accent) : colors.muted));
            std::wstring digit = (m_theme == CandidateTheme::Terminal || cyber ? L"0" : L"") +
                std::to_wstring(i + 1) + (m_theme == CandidateTheme::System ? L"." : L"");
            DrawTextW(dc, digit.c_str(), -1, &number, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(dc, itemFont);
            SetTextColor(dc, ink);
            const int textRight = WIN_W - (cyber ? 27 : PADDING + 4);
            RECT candidate = { PADDING + 35, top, textRight, top + ITEM_H };
            DrawTextW(dc, m_candidates[i].text.c_str(), -1, &candidate,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (!m_candidates[i].annotation.empty()) {
                SIZE wordSize = {};
                GetTextExtentPoint32W(dc, m_candidates[i].text.c_str(),
                    static_cast<int>(m_candidates[i].text.size()), &wordSize);
                int noteLeft = candidate.left + wordSize.cx + 7;
                if (noteLeft < textRight - 30) {
                    SelectObject(dc, smallFont);
                    SetTextColor(dc, noteInk);
                    RECT note = { noteLeft, top, textRight, top + ITEM_H };
                    DrawTextW(dc, m_candidates[i].annotation.c_str(), -1, &note,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    SelectObject(dc, itemFont);
                }
            }
        }
    }

    int footerTop = bounds.bottom - FOOTER_H;
    line = { 1, footerTop, WIN_W - 1, footerTop + 1 };
    Fill(dc, line, colors.line);
    SelectObject(dc, m_fonts[3]);
    SetTextColor(dc, !m_loading && m_totalPages > 1 ? colors.accent : colors.muted);
    RECT previous = { PADDING, footerTop + 1, 48, bounds.bottom - 1 };
    RECT next = { WIN_W - 48, footerTop + 1, WIN_W - PADDING, bounds.bottom - 1 };
    if (m_theme == CandidateTheme::GameBoy) {
        Panel(dc, {12, footerTop + 5, 43, bounds.bottom - 5}, colors.surface, colors.line);
        Panel(dc, {WIN_W - 43, footerTop + 5, WIN_W - 12, bounds.bottom - 5}, colors.surface, colors.line);
    }
    if (!iconFree && !IsDavidLucyTheme(m_theme)) {
        DrawTextW(dc, m_theme == CandidateTheme::Terminal ? L"[-]" : L"‹", -1, &previous, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(dc, m_theme == CandidateTheme::Terminal ? L"[=]" : L"›", -1, &next, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    wchar_t pageText[48];
    if (m_loading) wcscpy_s(pageText, m_selectionPending ? L"已收到選字 · 等候中" : L"正在搜尋…");
    else if (cyber) swprintf_s(pageText, L"%02d / %02d", m_page, m_totalPages);
    else swprintf_s(pageText, L"%d / %d", m_page, m_totalPages);
    RECT page = { 48, footerTop + 1, WIN_W - 48, bounds.bottom - 1 };
    SetTextColor(dc, colors.muted);
    DrawTextW(dc, pageText, -1, &page, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (!iconFree && IsDavidLucyTheme(m_theme))
        EarthMoonRoute(dc, WIN_W, footerTop, FOOTER_H, m_page, m_totalPages, m_loading);

    HPEN border = CreatePen(PS_SOLID, 1, colors.line);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, border));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    Rectangle(dc, 0, 0, bounds.right, bounds.bottom);
    if (edgerunner) {
        // Inner bevel: cut the content's four corners parallel to the bezel and trace them in the line colour.
        constexpr int cut = 10;
        const int right = bounds.right - 1, bottom = bounds.bottom - 1;
        HBRUSH panel = CreateSolidBrush(chrome.panel);
        for (int i = 0; i < cut; ++i) {
            const RECT rows[] = {{0, i, cut - i, i + 1}, {right - cut + i + 1, i, right + 1, i + 1},
                {0, bottom - i, cut - i, bottom - i + 1}, {right - cut + i + 1, bottom - i, right + 1, bottom - i + 1}};
            for (const RECT& row : rows) FillRect(dc, &row, panel);
        }
        DeleteObject(panel);
        for (int i = 0; i <= cut; ++i) {
            SetPixelV(dc, cut - i, i, colors.line);
            SetPixelV(dc, right - cut + i, i, colors.line);
            SetPixelV(dc, cut - i, bottom - i, colors.line);
            SetPixelV(dc, right - cut + i, bottom - i, colors.line);
        }
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(border);
    SelectObject(dc, original);
    SetViewportOrgEx(dc, origin.x, origin.y, nullptr);
}

void CandidateWindow::Click(int x, int y) {
    const Layout layout = GetLayout();
    x -= layout.content.left; y -= layout.content.top;
    if (m_loading || x < 0 || x >= layout.content.right - layout.content.left ||
        y < layout.header || y >= layout.content.bottom - layout.content.top) return;
    const int row = (y - layout.header) / layout.row;
    if (row >= 0 && row < static_cast<int>(m_candidates.size())) {
        if (m_cb) m_cb(row);
    } else if (y >= layout.content.bottom - layout.content.top - layout.footer && m_totalPages > 1 && m_pageCb) {
        if (x < 48) m_pageCb(-1);
        else if (x >= layout.content.right - layout.content.left - 48) m_pageCb(1);
    }
}

LRESULT CALLBACK CandidateWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<CandidateWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    switch (msg) {
    case WM_PAINT:
        if (self) self->Paint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        if (self && LOWORD(lp) == HTCLIENT && !self->m_loading && self->m_totalPages > 1 && self->m_pageCb) {
            POINT point;
            GetCursorPos(&point); ScreenToClient(hwnd, &point);
            const Layout layout = self->GetLayout();
            const int x = point.x - layout.content.left;
            if (point.y >= layout.content.bottom - layout.footer && point.y < layout.content.bottom &&
                x >= 0 && x < layout.content.right - layout.content.left &&
                (x < 48 || x >= layout.content.right - layout.content.left - 48)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        if (self) {
            const Layout layout = self->GetLayout();
            int y = GET_Y_LPARAM(lp) - layout.content.top;
            int x = GET_X_LPARAM(lp);
            int row = (y - layout.header) / layout.row;
            if (self->m_loading || x < layout.content.left || x >= layout.content.right ||
                y < layout.header || row >= static_cast<int>(self->m_candidates.size())) row = -1;
            if (row != self->m_hoverRow) {
                self->m_hoverRow = row;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&track);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (self) { self->m_hoverRow = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONUP:
        if (self) self->Click(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
