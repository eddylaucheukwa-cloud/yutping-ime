#pragma once
#include "common.h"
#include "UserSettings.h"
#include <vector>
#include <string>
#include <functional>

using SelectCallback = std::function<void(int index)>;
using PageCallback = std::function<void(int delta)>;

// Lightweight topmost popup window showing candidate list.
// Follows the caret position on screen.
class CandidateWindow {
public:
    CandidateWindow();
    ~CandidateWindow();

    // Call once to register the window class
    static bool RegisterClass(HINSTANCE hInst);

    // Show / update candidates at screen position (x, y = caret bottom-left)
    void Show(const std::wstring& composition,
              const std::vector<Candidate>& candidates, POINT caretPt,
              int page, int totalPages, bool loading, bool cached, bool offline, int selectedRow = 0);
    void Hide();

    bool IsVisible() const;
    void SetTheme(CandidateTheme theme);
    // Visual feedback only; the owner calls this after a spelling key is accepted.
    void NotifyTyping();
    void SetSelectionPending(bool pending) { m_selectionPending = pending; }
    SIZE PreferredSize() const { return GetLayout().size; }

    // Called when user presses a digit key — owner handles commit
    void SetSelectCallback(SelectCallback cb) { m_cb = cb; }
    void SetPageCallback(PageCallback cb) { m_pageCb = cb; }

private:
    friend class CandidateThemeTest;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void Paint(HWND hwnd);
    void StopGlow();
    void Draw(HDC dc, RECT bounds, bool dark);
    void DrawBackground(HDC dc, RECT bounds, COLORREF surface);
    void DrawThemeMark(HDC dc, RECT area);
    void CacheLetterMetrics(HDC dc);
    int DrawLetters(HDC dc, const std::wstring& text, RECT area, bool paint = true, bool gradient = true);
    int DrawGradientLetters(HDC dc, const std::wstring& text, RECT area);
    void DrawSelectionPlate(HDC dc, int left, int top, int width, int height, COLORREF solid);
    // Window outline for bevelled themes (nullptr keeps the plain rectangle).
    static HRGN BevelRegion(CandidateTheme theme, SIZE size);
    struct Layout { SIZE size; RECT content; int header, row, footer; };
    Layout GetLayout() const;
    void Click(int x, int y);

    HWND m_hwnd = nullptr;
    std::vector<Candidate> m_candidates;
    std::wstring m_composition;
    int m_page = 1;
    int m_totalPages = 1;
    int m_hoverRow = -1;
    int m_selectedRow = 0;
    bool m_loading = false;
    bool m_cached = false;
    bool m_offline = false;
    bool m_selectionPending = false;
    CandidateTheme m_theme = CandidateTheme::System;
    CandidateTheme m_fontTheme = CandidateTheme::System;
    HFONT m_fonts[5] = {}; // Original fonts 0-3; private composition letters 4.
    struct LetterMetrics {
        ABCFLOAT widths[26] = {};
        int kern[26][26] = {};
        TEXTMETRICW text = {};
        float space = 0;
    } m_letters;
    HANDLE m_letterResource = nullptr;
    bool m_letterResourceAttempted = false;
    int m_fontRowHeight = 0;
    HBITMAP m_background = nullptr;
    HBITMAP m_glowBackground = nullptr;
    bool m_glowActive = false;
    ULONGLONG m_lastTyping = 0;
    static constexpr UINT_PTR GLOW_TIMER = 1;
    static constexpr UINT GLOW_IDLE_MS = 500;
    SIZE m_backgroundSize = {};
    CandidateTheme m_backgroundTheme = CandidateTheme::System;
    HBITMAP m_mark = nullptr;
    CandidateTheme m_markTheme = CandidateTheme::System;
    HBITMAP m_gradientTitle = nullptr;
    SIZE m_gradientTitleSize = {};
    std::wstring m_gradientTitleText;
    CandidateTheme m_gradientTitleTheme = CandidateTheme::System;
    int m_gradientTitleAdvance = 0;
    HBITMAP m_plate = nullptr;
    SIZE m_plateSize = {};
    CandidateTheme m_plateTheme = CandidateTheme::System;
    SIZE m_shapeSize = {};
    CandidateTheme m_shapeTheme = CandidateTheme::System;
    SelectCallback m_cb;
    PageCallback m_pageCb;
    static HINSTANCE s_hInst;

    static std::wstring s_className;
    static constexpr int HEADER_H = 42;
    static constexpr int ITEM_H = 34;
    static constexpr int FOOTER_H = 32;
    static constexpr int WIN_W = 248;
    static constexpr int PADDING = 8;
};
