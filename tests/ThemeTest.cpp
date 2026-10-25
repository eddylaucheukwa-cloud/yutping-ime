#include "CandidateWindow.h"
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>

static void Check(bool okay, const char* message) { if (!okay) throw std::runtime_error(message); }
static double Luminance(COLORREF color) {
    auto linear = [](int value) { double n = value / 255.0; return n <= 0.04045 ? n / 12.92 : std::pow((n + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(GetRValue(color)) + 0.7152 * linear(GetGValue(color)) + 0.0722 * linear(GetBValue(color));
}
static double Contrast(COLORREF a, COLORREF b) {
    double x = Luminance(a), y = Luminance(b);
    return ((std::max)(x, y) + 0.05) / ((std::min)(x, y) + 0.05);
}

class CandidateThemeTest {
public:
    static void Run(const wchar_t* output, HKEY sandbox) {
        const int width = 1184, height = output ? 1360 : 1040;
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        Check(bitmap && pixels, "render bitmap");
        HGDIOBJ original = SelectObject(dc, bitmap);
        HBRUSH background = CreateSolidBrush(RGB(235, 232, 225));
        RECT canvas = {0, 0, width, height}; FillRect(dc, &canvas, background); DeleteObject(background);
        HFONT label = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        const wchar_t* labels[] = {L"SYSTEM / LIGHT", L"SYSTEM / DARK", L"TERMINAL", L"GAME BOY", L"LUCY · AFTERGLOW", L"DAVID & LUCY · EARTHRISE", L"LUCY · FLOW", L"DAVID & LUCY · EMBRACE", L"DAVID & LUCY · BLUE MARBLE"};
        const CandidateTheme themes[] = {CandidateTheme::System, CandidateTheme::System, CandidateTheme::Terminal, CandidateTheme::GameBoy,
            CandidateTheme::Lucy, CandidateTheme::DavidLucy, CandidateTheme::LucyFlow, CandidateTheme::DavidLucyEmbrace, CandidateTheme::Earth};
        CandidateWindow preview;
        preview.m_composition = L"neiho";
        preview.m_candidates = {{L"你好", L"nei hou", 5}, {L"你可", L"nei ho", 5},
            {L"您可", L"nei ho", 5}, {L"您好", L"nei hou", 5}, {L"妳好", L"nei hou", 5}, {L"你", L"nei", 3}};
        preview.m_totalPages = 3;
        {
            UserSettings retired; retired.theme = static_cast<CandidateTheme>(3);
            Check(RegOverridePredefKey(HKEY_CURRENT_USER, sandbox) == ERROR_SUCCESS, "settings sandbox");
            const bool retiredFallsBack = SaveUserSettings(retired) && LoadUserSettings().theme == CandidateTheme::System;
            RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
            Check(retiredFallsBack, "retired Paper & Ink theme falls back safely instead of selecting another theme");
            retired.theme = static_cast<CandidateTheme>(4);
            Check(RegOverridePredefKey(HKEY_CURRENT_USER, sandbox) == ERROR_SUCCESS, "settings sandbox");
            const bool neonFallsBack = SaveUserSettings(retired) && LoadUserSettings().theme == CandidateTheme::System;
            RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
            Check(neonFallsBack, "removed Neon Arcade theme falls back to System without renumbering other themes");
            retired.theme = static_cast<CandidateTheme>(999);
            Check(RegOverridePredefKey(HKEY_CURRENT_USER, sandbox) == ERROR_SUCCESS, "settings sandbox");
            const bool invalidFallsBack = SaveUserSettings(retired) && LoadUserSettings().theme == CandidateTheme::System;
            RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
            Check(invalidFallsBack, "unknown theme IDs fall back safely");
            retired.theme = static_cast<CandidateTheme>(8);
            Check(RegOverridePredefKey(HKEY_CURRENT_USER, sandbox) == ERROR_SUCCESS, "settings sandbox");
            const bool withdrawnUsesLucy = SaveUserSettings(retired) && LoadUserSettings().theme == CandidateTheme::Lucy;
            RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
            Check(withdrawnUsesLucy, "withdrawn Flow variant returns to the previous Lucy theme");
            retired.theme = static_cast<CandidateTheme>(6);
            Check(RegOverridePredefKey(HKEY_CURRENT_USER, sandbox) == ERROR_SUCCESS, "settings sandbox");
            const bool davidFallsBack = SaveUserSettings(retired) && LoadUserSettings().theme == CandidateTheme::System;
            RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
            Check(davidFallsBack, "removed David theme falls back to System without renumbering other themes");
        }
        for (int i = 0; i < static_cast<int>(std::size(themes)); ++i) {
            preview.SetTheme(themes[i]);
            const int x = 24 + (i % 4) * 292, y = 48 + (i / 4) * 338;
            SetViewportOrgEx(dc, x, y, nullptr);
            HGDIOBJ oldFont = SelectObject(dc, label);
            SetTextColor(dc, RGB(57, 55, 49)); SetBkMode(dc, TRANSPARENT);
            TextOutW(dc, 0, -26, labels[i], static_cast<int>(wcslen(labels[i])));
            SelectObject(dc, oldFont);
            const SIZE size = preview.PreferredSize();
            preview.Draw(dc, {0, 0, size.cx, size.cy}, i == 1);
            HFONT cachedFont = preview.m_fonts[0];
            HFONT cachedLetters = preview.m_fonts[4];
            HBITMAP cachedBackground = preview.m_background;
            HBITMAP cachedMark = preview.m_mark;
            HBITMAP cachedGradient = preview.m_gradientTitle;
            preview.Draw(dc, {0, 0, size.cx, size.cy}, i == 1);
            Check(cachedFont && cachedFont == preview.m_fonts[0], "fonts are reused between repaints");
            Check(cachedLetters == preview.m_fonts[4], "private letter font is reused between repaints");
            if (preview.m_theme == CandidateTheme::Lucy || preview.m_theme == CandidateTheme::LucyFlow) {
                Check(cachedGradient && cachedGradient == preview.m_gradientTitle,
                    "Lucy composition gradient is cached between repaints");
                DIBSECTION title = {};
                Check(GetObjectW(cachedGradient, sizeof(title), &title) && title.dsBm.bmBits,
                    "Lucy gradient title has a readable glyph mask");
                const auto* pixels = static_cast<const std::uint32_t*>(title.dsBm.bmBits);
                int lavender = 0, mint = 0;
                for (int pixel = 0; pixel < title.dsBm.bmWidth * title.dsBm.bmHeight; ++pixel) {
                    const unsigned value = pixels[pixel];
                    const unsigned alpha = value >> 24;
                    if (alpha < 128) continue;
                    const int r = ((value >> 16) & 255) * 255 / alpha;
                    const int g = ((value >> 8) & 255) * 255 / alpha, b = (value & 255) * 255 / alpha;
                    lavender += r > g && b > g;
                    mint += (g > r + 25 && g > b) || (b > r + 25 && b > g);
                }
                Check(lavender && mint, "Lucy composition letters follow her hair from lavender into cyan and mint");
                for (int x = 0; x < title.dsBm.bmWidth; ++x) {
                    int minRed = 255, maxRed = 0, count = 0;
                    for (int y = 0; y < title.dsBm.bmHeight; ++y) {
                        const unsigned value = pixels[y * title.dsBm.bmWidth + x], alpha = value >> 24;
                        if (alpha < 240) continue;
                        const int red = ((value >> 16) & 255) * 255 / alpha;
                        minRed = (std::min)(minRed, red); maxRed = (std::max)(maxRed, red); ++count;
                    }
                    Check(count < 2 || maxRed - minRed <= 2, "Lucy title gradient runs horizontally rather than vertically");
                }
            }
            if (i >= 4) {
                Check(preview.m_letterResource && preview.m_fonts[4], "cyber composition font is embedded and loaded privately");
                HGDIOBJ previous = SelectObject(dc, preview.m_fonts[4]);
                wchar_t face[LF_FACESIZE] = {};
                GetTextFaceW(dc, LF_FACESIZE, face);
                Check(wcscmp(face, L"Yutping Cyber Reference Study") == 0, "approved custom font is selected, not a fallback");
                SIZE unspaced = {};
                GetTextExtentPoint32W(dc, L"neiho", 5, &unspaced);
                LOGFONTW custom = {};
                GetObjectW(preview.m_fonts[4], sizeof(custom), &custom);
                Check(custom.lfWeight == FW_NORMAL, "composition font is not synthetically emboldened");
                SelectObject(dc, preview.m_fonts[0]);
                const RECT headingArea = {0, 0, 1000, preview.GetLayout().header};
                const int compact = preview.DrawLetters(dc, L"neiho", headingArea, false);
                Check(compact < unspaced.cx, "native composition rendering applies the approved tight letter spacing");
                SIZE digits = {};
                GetTextExtentPoint32W(dc, L"012345", 6, &digits);
                Check(preview.DrawLetters(dc, L"012345", headingArea, false) == digits.cx, "composition digits retain the existing font and width");
                SelectObject(dc, preview.m_fonts[2]);
                GetTextFaceW(dc, LF_FACESIZE, face);
                Check(wcscmp(face, L"Consolas") == 0, "candidate annotations and row numbers retain their original font");
                SelectObject(dc, preview.m_fonts[3]);
                GetTextFaceW(dc, LF_FACESIZE, face);
                Check(wcscmp(face, L"Consolas") == 0, "page numbers retain their original font");
                SelectObject(dc, previous);
            } else Check(!preview.m_fonts[4], "other themes retain their original composition fonts");
            if (preview.m_theme == CandidateTheme::Terminal)
                Check(cachedMark && cachedMark == preview.m_mark, "detailed theme icons are reused between repaints");
            else if (preview.m_theme == CandidateTheme::Lucy || preview.m_theme == CandidateTheme::LucyFlow ||
                preview.m_theme == CandidateTheme::DavidLucy ||
                preview.m_theme == CandidateTheme::DavidLucyEmbrace || preview.m_theme == CandidateTheme::Earth) {
                Check(!preview.m_mark, "icon-free themes discard any previous theme's cached icon");
                const auto content = preview.GetLayout();
                const int footerY = content.content.bottom - content.content.top - content.footer / 2;
                HDC photograph = CreateCompatibleDC(dc);
                HGDIOBJ oldPhoto = SelectObject(photograph, preview.m_background);
                for (int x : {28, 220})
                    Check(GetPixel(dc, content.content.left + x, content.content.top + footerY) ==
                        GetPixel(photograph, x, footerY), "icon-free footer leaves the background visible where paging icons used to be");
                SelectObject(photograph, oldPhoto); DeleteDC(photograph);
            }
            const auto started = std::chrono::steady_clock::now();
            for (int frame = 0; frame < 20; ++frame) preview.Draw(dc, {0, 0, size.cx, size.cy}, i == 1);
            GdiFlush();
            const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() / 20;
            std::cout << "theme " << i << " repaint: " << milliseconds << " ms\n";
            const auto layout = preview.GetLayout();
            LOGFONTW heading = {}, footer = {};
            Check(GetObjectW(preview.m_fonts[0], sizeof(heading), &heading) && GetObjectW(preview.m_fonts[3], sizeof(footer), &footer), "theme fonts exist");
            Check(wcscmp(heading.lfFaceName, footer.lfFaceName) == 0 && heading.lfWeight == footer.lfWeight && heading.lfItalic == footer.lfItalic,
                "page numbers and navigation match the heading font family and weight");
            if (preview.m_theme == CandidateTheme::Terminal) {
                for (int y = 0; y < size.cy; ++y) for (int x = 0; x < size.cx; ++x) {
                    const COLORREF pixel = GetPixel(dc, x, y);
                    Check(GetRValue(pixel) == GetGValue(pixel) && GetGValue(pixel) == GetBValue(pixel),
                        "Terminal renders only black, white and gray, including antialiased glyphs");
                }
            }
            {
                HGDIOBJ previous = SelectObject(dc, preview.m_fonts[0]);
                wchar_t face[LF_FACESIZE] = {};
                GetTextFaceW(dc, LF_FACESIZE, face);
                std::wcout << L"theme " << i << L" heading font: " << face << L'\n';
                if (preview.m_theme != CandidateTheme::System) {
                    SIZE narrow = {}, wide = {};
                    GetTextExtentPoint32W(dc, L"iii", 3, &narrow);
                    GetTextExtentPoint32W(dc, L"WWW", 3, &wide);
                    Check(narrow.cx == wide.cx, "terminal, LCD and arcade fonts must be monospaced");
                }
                SelectObject(dc, previous);
            }
            for (int fontIndex : {1, 2, 3}) {
                HGDIOBJ previous = SelectObject(dc, preview.m_fonts[fontIndex]);
                TEXTMETRICW metrics = {};
                Check(GetTextMetricsW(dc, &metrics) && metrics.tmHeight <= layout.row - 4, "candidate fonts fit without shrinking for nine rows");
                SelectObject(dc, previous);
            }
            for (int row : {0, 2}) {
                const int top = layout.content.top + layout.header + row * layout.row;
                HGDIOBJ previous = SelectObject(dc, preview.m_fonts[1]);
                SIZE word = {}, note = {};
                const Candidate& candidate = preview.m_candidates[row];
                GetTextExtentPoint32W(dc, candidate.text.c_str(), static_cast<int>(candidate.text.size()), &word);
                SelectObject(dc, preview.m_fonts[2]);
                GetTextExtentPoint32W(dc, candidate.annotation.c_str(), static_cast<int>(candidate.annotation.size()), &note);
                SelectObject(dc, previous);
                const int left = layout.content.left + 43 + word.cx + 7;
                // Background just left of the note: the highlight fades, so the far right is not representative.
                const COLORREF backgroundColor = GetPixel(dc, left - 3, top + layout.row / 2);
                double contrast = 1;
                // Measure actual annotation glyphs: selected text may be inverted.
                for (int y = top + 7; y < top + layout.row - 7; ++y)
                    for (int x = left; x < left + note.cx; ++x)
                        contrast = (std::max)(contrast, Contrast(GetPixel(dc, x, y), backgroundColor));
                std::cout << "theme " << i << " row " << row << " annotation contrast: " << contrast << '\n';
                Check(contrast >= 4.5, "rendered annotation contrast below 4.5:1");
            }
            const int probeX = layout.content.right - 32;
            const int firstY = layout.content.top + layout.header + layout.row / 2;
            const COLORREF selectedFill = GetPixel(dc, probeX, firstY);
            const COLORREF normalFill = GetPixel(dc, probeX, firstY + layout.row * 2);
            Check(selectedFill != normalFill, "selected candidate has a visible highlight");
            if (preview.m_theme == CandidateTheme::Lucy || preview.m_theme == CandidateTheme::LucyFlow)
            {
                const COLORREF plateStart = GetPixel(dc, layout.content.left + 9, firstY);
                Check(GetRValue(plateStart) > GetGValue(plateStart) && GetBValue(plateStart) > GetGValue(plateStart) &&
                    GetGValue(selectedFill) > GetRValue(selectedFill) && GetGValue(selectedFill) > GetBValue(selectedFill),
                    "Lucy highlight runs from lavender at the number to mint at the right, like her hair");
            }
            if (preview.m_theme == CandidateTheme::Lucy || preview.m_theme == CandidateTheme::LucyFlow) {
                Check(cachedBackground && cachedBackground == preview.m_background,
                    "embedded Lucy background is decoded once and reused between repaints");
                Check(GetPixel(dc, layout.content.left + 170, firstY + layout.row * 2) !=
                    GetPixel(dc, layout.content.left + 170, firstY + layout.row * 4),
                    "Lucy hair is visible behind the candidate rows");
                Check(!preview.m_mark, "both Lucy themes retain the background without a header icon");
            }
            if (preview.m_theme == CandidateTheme::DavidLucyEmbrace || preview.m_theme == CandidateTheme::Earth) {
                Check(cachedBackground && cachedBackground == preview.m_background,
                    "combined Earth and embrace background is cached between repaints");
                HDC sample = CreateCompatibleDC(dc);
                HGDIOBJ oldSample = SelectObject(sample, preview.m_background);
                int photoPixels = 0;
                for (int y = 2; y < layout.header - 2; ++y)
                    for (int x = 8; x < layout.content.right - layout.content.left - 8; ++x)
                        photoPixels += GetPixel(sample, x, y) != RGB(18, 17, 34);
                Check(photoPixels > 500, "Earth photograph extends through the header instead of stopping below it");
                const COLORREF expected = GetPixel(sample, 188, 8);
                const COLORREF actual = GetPixel(dc, layout.content.left + 188, layout.content.top + 8);
                Check(std::abs(GetRValue(actual) - GetRValue(expected)) <= 3 &&
                    std::abs(GetGValue(actual) - GetGValue(expected)) <= 3 &&
                    std::abs(GetBValue(actual) - GetBValue(expected)) <= 3,
                    "icon cache preserves the underlying Earth photograph without an opaque box");
                if (preview.m_theme == CandidateTheme::Earth) {
                    CandidateWindow embrace;
                    embrace.SetTheme(CandidateTheme::DavidLucyEmbrace);
                    embrace.m_candidates = preview.m_candidates;
                    HDC figure = CreateCompatibleDC(dc);
                    HBITMAP buffer = CreateCompatibleBitmap(dc, size.cx, size.cy);
                    HGDIOBJ oldBuffer = SelectObject(figure, buffer);
                    embrace.Draw(figure, {0, 0, size.cx, size.cy}, false);
                    HGDIOBJ previous = SelectObject(figure, embrace.m_background);
                    int changed = 0;
                    for (int y = 64; y < 220; y += 3)
                        for (int x = 50; x < 200; x += 3)
                            changed += GetPixel(sample, x, y) != GetPixel(figure, x, y);
                    Check(changed > 500, "Earth-only background removes the embrace figure instead of reusing it");
                    Check(GetPixel(sample, 120, 30) == GetPixel(figure, 120, 30),
                        "Earth-only theme keeps the same large globe position and opacity");
                    int footerFigure = 0;
                    const int footerTop = layout.content.bottom - layout.content.top - layout.footer;
                    for (int y = footerTop + 2; y < footerTop + layout.footer - 2; y += 2)
                        for (int x = 60; x < 190; x += 2)
                            footerFigure += GetPixel(sample, x, y) != GetPixel(figure, x, y);
                    Check(footerFigure > 100, "embrace artwork continues behind the page footer instead of being clipped above it");
                    Check(GetPixel(dc, layout.content.left + 74, layout.content.top + footerTop + 6) ==
                        GetPixel(sample, 74, footerTop + 6), "page footer preserves its underlying photograph");
                    SelectObject(figure, previous);
                    SelectObject(figure, oldBuffer); DeleteObject(buffer); DeleteDC(figure);
                }
                SelectObject(sample, oldSample); DeleteDC(sample);
            }
            if (preview.m_theme == CandidateTheme::DavidLucy) {
                Check(!preview.m_mark, "David & Lucy no longer draws a header mark");
                Check(cachedBackground && cachedBackground == preview.m_background, "embedded Earth image is decoded once and reused between repaints");
                Check(GetPixel(dc, layout.content.left + 170, firstY + layout.row * 2) !=
                    GetPixel(dc, layout.content.left + 170, firstY + layout.row * 4), "Earth photograph is visible behind the candidate rows");
            }
            if (preview.m_theme == CandidateTheme::DavidLucy) {
                Check(GetGValue(selectedFill) > GetRValue(selectedFill) && GetGValue(selectedFill) > GetBValue(selectedFill),
                    "David & Lucy keeps the reference scene's green rim light");
                const int footerY = layout.content.bottom - layout.footer + layout.footer / 2;
                HDC backgroundDC = CreateCompatibleDC(dc);
                HGDIOBJ oldBackground = SelectObject(backgroundDC, preview.m_background);
                const int footerRow = footerY - layout.content.top;
                const COLORREF earthPixel = GetPixel(backgroundDC, 28, footerRow);
                const COLORREF moonPixel = GetPixel(backgroundDC, layout.content.right - layout.content.left - 28, footerRow);
                Check(GetPixel(dc, layout.content.left + 28, footerY) == earthPixel &&
                    GetPixel(dc, layout.content.right - 28, footerY) == moonPixel,
                    "Earth and Moon paging icons are removed; the footer shows the photograph");
                const int contentWidth = layout.content.right - layout.content.left;
                const int contentHeight = layout.content.bottom - layout.content.top;
                const COLORREF surface = GetPixel(backgroundDC, 0, 0);
                int earthLeft = contentWidth, earthRight = -1, earthTop = contentHeight, earthBottom = -1;
                // Inspect the photograph itself, excluding the separate lunar foreground.
                for (int y = layout.header; y < contentHeight - (std::max)(40, contentHeight / 5); ++y)
                    for (int x = 1; x < contentWidth - 1; ++x) if (GetPixel(backgroundDC, x, y) != surface) {
                        earthLeft = (std::min)(earthLeft, x); earthRight = (std::max)(earthRight, x);
                        earthTop = (std::min)(earthTop, y); earthBottom = (std::max)(earthBottom, y);
                    }
                Check(earthRight > earthLeft && earthRight - earthLeft < contentWidth * 2 / 3,
                    "background Earth remains smaller than the candidate panel");
                Check(std::abs(earthLeft + earthRight + 1 - contentWidth) <= 12 &&
                    std::abs(earthTop + earthBottom + 1 - contentHeight) <= 12,
                    "visible Earth is centered in the panel rather than aligned to the right");
                for (int x : {11L, layout.content.right - layout.content.left - 12}) {
                    const int y = footerY - layout.content.top - layout.footer / 2 + 5;
                    Check(GetPixel(dc, layout.content.left + x, layout.content.top + y) == GetPixel(backgroundDC, x, y),
                        "David & Lucy does not retain the old keycap borders");
                }
                SelectObject(backgroundDC, oldBackground); DeleteDC(backgroundDC);
                {
                    CandidateWindow resized;
                    resized.SetTheme(CandidateTheme::DavidLucy);
                    HDC scratch = CreateCompatibleDC(dc);
                    HBITMAP scratchBitmap = CreateCompatibleBitmap(dc, contentWidth, 36 + 9 * 32 + 28);
                    HGDIOBJ oldScratch = SelectObject(scratch, scratchBitmap);
                    std::vector<COLORREF> sixRowEarth;
                    for (int rows : {6, 1, 2, 3, 4, 9}) {
                        resized.m_candidates.resize(rows, {L"字", L"", 1});
                        const auto resizedLayout = resized.GetLayout();
                        const int height = resizedLayout.content.bottom - resizedLayout.content.top;
                        resized.DrawBackground(scratch, {0, 0, contentWidth, height}, surface);
                        Check(resized.m_background, "resized Earth background exists");
                        HDC sample = CreateCompatibleDC(dc);
                        HGDIOBJ oldSample = SelectObject(sample, resized.m_background);
                        std::vector<COLORREF> pixels;
                        // Shorter panels must retain the same top part of the photo, above the lunar foreground.
                        const int sampleBottom = (std::min)(152, height - (std::max)(40, height / 5) - 1);
                        for (int y = 4; y <= sampleBottom; y += 4) for (int x = -40; x <= 40; x += 4)
                            pixels.push_back(GetPixel(sample, contentWidth / 2 + x, y));
                        SelectObject(sample, oldSample); DeleteDC(sample);
                        if (rows == 6) {
                            sixRowEarth = pixels;
                            Check(std::count_if(pixels.begin(), pixels.end(), [surface](COLORREF pixel) { return pixel != surface; }) > 10,
                                "Earth comparison samples contain photograph detail");
                        } else {
                            int differences = 0, largestDifference = 0;
                            for (size_t pixel = 0; pixel < pixels.size(); ++pixel) {
                                if (pixels[pixel] != sixRowEarth[pixel]) ++differences;
                                largestDifference = (std::max)({largestDifference,
                                    std::abs(static_cast<int>(GetRValue(pixels[pixel])) - GetRValue(sixRowEarth[pixel])),
                                    std::abs(static_cast<int>(GetGValue(pixels[pixel])) - GetGValue(sixRowEarth[pixel])),
                                    std::abs(static_cast<int>(GetBValue(pixels[pixel])) - GetBValue(sixRowEarth[pixel]))});
                            }
                            std::cout << "Earth rows=" << rows << " changed pixels=" << differences << " max channel difference=" << largestDifference << '\n';
                            // Clipping a partially offscreen GDI+ image can round a channel by one level.
                            Check(largestDifference <= 1, "Earth stays fixed at the top while shorter panels crop from the bottom");
                        }
                    }
                    SelectObject(scratch, oldScratch); DeleteObject(scratchBitmap); DeleteDC(scratch);
                }
                preview.m_page = preview.m_totalPages = 123456;
                preview.Draw(dc, {0, 0, size.cx, size.cy}, i == 1);
                Check(GetPixel(dc, layout.content.left + 28, footerY) == earthPixel &&
                    GetPixel(dc, layout.content.right - 28, footerY) == moonPixel,
                    "long page counts stay centred and leave the footer edges clear");
                preview.m_page = 1; preview.m_totalPages = 3;
                preview.SetTheme(CandidateTheme::Lucy);
                preview.Draw(dc, {0, 0, size.cx, size.cy}, false);
                Check(preview.m_background && preview.m_backgroundTheme == CandidateTheme::Lucy,
                    "switching from the combined theme replaces Earth with Lucy hair");
                preview.SetTheme(CandidateTheme::DavidLucy);
                preview.Draw(dc, {0, 0, size.cx, size.cy}, false);
                Check(preview.m_background, "returning to the combined theme recreates its Earth background");
            }
            preview.m_selectedRow = 1;
            preview.m_loading = true;
            preview.Draw(dc, {0, 0, size.cx, size.cy}, i == 1);
            const COLORREF normalFirst = GetPixel(dc, probeX, firstY);
            const COLORREF normalSecond = GetPixel(dc, probeX, firstY + layout.row);
            preview.m_loading = false;
            preview.Draw(dc, {0, 0, size.cx, size.cy}, i == 1);
            Check(GetPixel(dc, probeX, firstY) == normalFirst && GetPixel(dc, probeX, firstY + layout.row) != normalSecond,
                "keyboard highlight moves from the first to the second candidate in every theme");
            preview.m_loading = true;
            preview.Draw(dc, {0, 0, size.cx, size.cy}, i == 1);
            Check(GetPixel(dc, probeX, firstY + layout.row) == normalSecond, "loading disables the previous keyboard highlight");
            preview.m_loading = false; preview.m_selectedRow = 0;
            preview.Draw(dc, {0, 0, size.cx, size.cy}, i == 1);
            UserSettings settings; settings.theme = preview.m_theme;
            Check(RegOverridePredefKey(HKEY_CURRENT_USER, sandbox) == ERROR_SUCCESS, "settings sandbox");
            Check(SaveUserSettings(settings) && LoadUserSettings().theme == settings.theme, "theme settings round trip");
            RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        }
        for (CandidateTheme theme : {CandidateTheme::Lucy, CandidateTheme::LucyFlow}) {
            CandidateWindow hair;
            hair.SetTheme(theme);
            HDC sample = CreateCompatibleDC(dc);
            std::vector<COLORREF> fullPage;
            constexpr COLORREF surface = RGB(13, 10, 24);
            for (int rows : {6, 2, 9}) {
                hair.m_candidates.resize(rows);
                const auto layout = hair.GetLayout();
                // Draw into a scratch surface so the exported sheet keeps the Large Earth preview.
                HDC scratch = CreateCompatibleDC(dc);
                HBITMAP scratchBitmap = CreateCompatibleBitmap(dc, 248, layout.content.bottom - layout.content.top);
                HGDIOBJ oldScratch = SelectObject(scratch, scratchBitmap);
                hair.DrawBackground(scratch, {0, 0, 248, layout.content.bottom - layout.content.top}, surface);
                SelectObject(scratch, oldScratch); DeleteObject(scratchBitmap); DeleteDC(scratch);
                Check(hair.m_background, "both Lucy hair assets are embedded and decode successfully");
                HGDIOBJ old = SelectObject(sample, hair.m_background);
                std::vector<COLORREF> pixels;
                for (int y = 48; y <= 112; y += 4) for (int x = 24; x <= 224; x += 4)
                    pixels.push_back(GetPixel(sample, x, y));
                SelectObject(sample, old);
                if (rows == 6) {
                    fullPage = pixels;
                    Check(std::count_if(pixels.begin(), pixels.end(), [surface](COLORREF p) { return p != surface; }) > 10,
                        "Lucy crop samples include visible hair detail");
                } else {
                    int difference = 0;
                    for (size_t i = 0; i < pixels.size(); ++i)
                        difference = (std::max)({difference,
                            std::abs(static_cast<int>(GetRValue(pixels[i])) - GetRValue(fullPage[i])),
                            std::abs(static_cast<int>(GetGValue(pixels[i])) - GetGValue(fullPage[i])),
                            std::abs(static_cast<int>(GetBValue(pixels[i])) - GetBValue(fullPage[i]))});
                    Check(difference <= 1, "Lucy backgrounds retain size and position when page length changes");
                }
            }
            DeleteDC(sample);
        }
        {
            // Bevelled themes cut the window along the bezel's edge line; other themes stay rectangular.
            constexpr int w = 256, h = 264;
            Check(!CandidateWindow::BevelRegion(CandidateTheme::Terminal, {w, h}), "other themes keep a rectangular window");
            for (CandidateTheme theme : {CandidateTheme::Lucy, CandidateTheme::LucyFlow, CandidateTheme::DavidLucy,
                CandidateTheme::DavidLucyEmbrace, CandidateTheme::Earth}) {
                HRGN region = CandidateWindow::BevelRegion(theme, {w, h});
                Check(region && PtInRegion(region, w / 2, h / 2) && !PtInRegion(region, 0, 0) &&
                    !PtInRegion(region, w - 1, 0) && !PtInRegion(region, 0, h - 1) && !PtInRegion(region, w - 1, h - 1),
                    "bevelled themes open all four corners of the window");
                for (int k = 0; k <= 12; ++k) {
                    Check(PtInRegion(region, 12 - k, k) && PtInRegion(region, w - 13 + k, k) &&
                        PtInRegion(region, w - 1 - k, h - 13 + k) && PtInRegion(region, 12 - k, h - 1 - k),
                        "the window keeps every pixel of the bezel's diagonal edge line");
                    if (k <= 11)
                        Check(!PtInRegion(region, 11 - k, k) && !PtInRegion(region, w - 12 + k, k) &&
                            !PtInRegion(region, w - 1 - k, h - 12 + k) && !PtInRegion(region, 11 - k, h - 1 - k),
                            "pixels outside the bevel are cut away");
                }
                DeleteObject(region);
            }
        }
        if (output) {
            CandidateWindow cropped;
            cropped.SetTheme(CandidateTheme::DavidLucy);
            cropped.m_composition = preview.m_composition;
            cropped.m_totalPages = 3;
            int column = 0;
            for (int rows : {2, 3, 6}) {
                cropped.m_candidates = preview.m_candidates;
                cropped.m_candidates.resize(rows);
                SetViewportOrgEx(dc, 24 + column++ * 292, 1058, nullptr);
                HGDIOBJ oldLabel = SelectObject(dc, label);
                SetTextColor(dc, RGB(57, 55, 49)); SetBkMode(dc, TRANSPARENT);
                const std::wstring caption = std::to_wstring(rows) + L" CANDIDATES / BOTTOM CROP";
                TextOutW(dc, 0, -26, caption.c_str(), static_cast<int>(caption.size()));
                SelectObject(dc, oldLabel);
                const SIZE size = cropped.PreferredSize();
                cropped.Draw(dc, {0, 0, size.cx, size.cy}, false);
            }
        }
        int selectedRow = -1, page = 0;
        preview.SetSelectCallback([&](int row) { selectedRow = row; });
        preview.SetPageCallback([&](int direction) { page += direction; });
        for (CandidateTheme theme : {CandidateTheme::System, CandidateTheme::Terminal, CandidateTheme::GameBoy,
                CandidateTheme::Lucy, CandidateTheme::LucyFlow, CandidateTheme::DavidLucy, CandidateTheme::DavidLucyEmbrace, CandidateTheme::Earth}) {
            preview.SetTheme(theme);
            for (int count : {0, 4, 6, 9}) {
                preview.m_candidates.resize(count, {L"字", L"", 1});
                const auto layout = preview.GetLayout();
                const int contentHeight = layout.content.bottom - layout.content.top;
                const int contentWidth = layout.content.right - layout.content.left;
                Check(layout.size.cx <= 256 && layout.size.cy <= (std::max)(116, 42 + count * 34 + 32 + 2),
                    "themes stay within the compact popup size budget");
                Check(layout.row >= 32 && layout.header + (std::max)(1, count) * layout.row + layout.footer == contentHeight,
                    "rows remain readable and fit the content");
                Check(layout.content.left >= 0 && layout.content.top >= 0 && layout.content.right <= layout.size.cx && layout.content.bottom <= layout.size.cy,
                    "theme decoration stays inside popup bounds");
                if (count == 6 && theme == CandidateTheme::GameBoy)
                    Check(layout.size.cx * layout.size.cy < 320 * 486 / 2, "Game Boy covers less than half its former area");
                auto click = [&](int x, int y) { preview.Click(layout.content.left + x, layout.content.top + y); };
                selectedRow = -1;
                click(55, layout.header / 2);
                Check(selectedRow == -1, "header is not a candidate");
                for (int row = 0; row < count; ++row) {
                    selectedRow = -1;
                    click(55, layout.header + layout.row * row + layout.row / 2);
                    Check(selectedRow == row, "every visible candidate is clickable across all themes");
                }
                selectedRow = -1; page = 0;
                click(20, contentHeight - layout.footer / 2);
                Check(page == -1 && selectedRow == -1, "footer previous page does not select a row");
                click(contentWidth - 20, contentHeight - layout.footer / 2);
                Check(page == 0, "footer next page");
                click(20, contentHeight + 1);
                click(-1, layout.header + 1);
                Check(page == 0 && selectedRow == -1, "outside popup content is inert");
                preview.m_loading = true;
                click(55, layout.header + layout.row / 2);
                click(contentWidth - 20, contentHeight - layout.footer / 2);
                Check(selectedRow == -1 && page == 0, "loading placeholders cannot commit stale candidates or change pages");
                Check(preview.PreferredSize().cy == layout.size.cy, "loading does not collapse existing rows");
                preview.m_loading = false;
            }
        }
        SetViewportOrgEx(dc, 0, 0, nullptr);
        GdiFlush();
        if (output) {
            ULONG_PTR token; Gdiplus::GdiplusStartupInput startup;
            Check(Gdiplus::GdiplusStartup(&token, &startup, nullptr) == Gdiplus::Ok, "PNG startup");
            {
                UINT count = 0, bytes = 0;
                Gdiplus::GetImageEncodersSize(&count, &bytes);
                std::vector<BYTE> storage(bytes);
                auto encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
                Check(Gdiplus::GetImageEncoders(count, bytes, encoders) == Gdiplus::Ok, "PNG encoder");
                bool saved = false;
                Gdiplus::Bitmap image(width, height, width * 4, PixelFormat32bppRGB, static_cast<BYTE*>(pixels));
                for (UINT i = 0; i < count; ++i) if (wcscmp(encoders[i].MimeType, L"image/png") == 0)
                    saved = image.Save(output, &encoders[i].Clsid, nullptr) == Gdiplus::Ok;
                Check(saved, "PNG export");
            }
            Gdiplus::GdiplusShutdown(token);
        }
        DeleteObject(label); SelectObject(dc, original); DeleteObject(bitmap); DeleteDC(dc);
    }
};

int wmain(int argc, wchar_t** argv) {
    HKEY real = nullptr, sandbox = nullptr;
    const std::wstring path = L"Software\\YutpingIME\\Tests\\Themes-" + std::to_wstring(GetCurrentProcessId());
    if (RegOpenCurrentUser(KEY_ALL_ACCESS, &real) != ERROR_SUCCESS ||
        RegCreateKeyExW(real, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &sandbox, nullptr) != ERROR_SUCCESS) return 2;
    int result = 0;
    try { CandidateThemeTest::Run(argc > 1 ? argv[1] : nullptr, sandbox); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    RegCloseKey(sandbox); RegDeleteTreeW(real, path.c_str()); RegCloseKey(real);
    return result;
}
