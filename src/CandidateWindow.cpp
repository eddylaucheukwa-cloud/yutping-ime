#include "CandidateWindow.h"
#include <windowsx.h>
#include <algorithm>
#include <cstdint>

HINSTANCE CandidateWindow::s_hInst = nullptr;
std::wstring CandidateWindow::s_className;

namespace {
struct Palette {
    COLORREF surface, ink, muted, line, selected, hover, accent;
};
constexpr Palette kLight = {
    RGB(255, 255, 255), RGB(32, 40, 51), RGB(108, 119, 132),
    RGB(224, 230, 237), RGB(232, 241, 255), RGB(245, 248, 252),
    RGB(48, 111, 217)
};
constexpr Palette kDark = {
    RGB(31, 36, 42), RGB(244, 246, 248), RGB(174, 186, 199),
    RGB(62, 73, 85), RGB(38, 62, 88), RGB(43, 53, 64),
    RGB(116, 181, 255)
};

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
} // namespace

CandidateWindow::CandidateWindow() {}
CandidateWindow::~CandidateWindow() {
    if (m_hwnd) DestroyWindow(m_hwnd);
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
                           int page, int totalPages, bool loading, bool cached, bool offline) {
    m_composition = composition;
    // While a newer API request is in flight, keep the previous rows as a
    // disabled visual placeholder. This keeps the popup from collapsing and
    // expanding on every typed letter.
    if (!loading || !candidates.empty() || m_candidates.empty()) {
        m_candidates = candidates;
        m_page = page;
        m_totalPages = totalPages;
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

    int rows = (std::max)(1, static_cast<int>(m_candidates.size()));
    int height = HEADER_H + ITEM_H * rows + FOOTER_H;
    MONITORINFO monitor = { sizeof(monitor) };
    if (!GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    const RECT& work = monitor.rcWork;
    int x = (std::max)(static_cast<int>(work.left),
        (std::min)(static_cast<int>(pt.x), static_cast<int>(work.right) - WIN_W));
    int y = pt.y + 4;
    if (y + height > work.bottom) y = pt.y - height - 4;
    y = (std::max)(static_cast<int>(work.top), y);

    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, WIN_W, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CandidateWindow::Hide() {
    if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
    m_candidates.clear();
    m_composition.clear();
    m_loading = false;
    m_cached = m_offline = false;
    m_hoverRow = -1;
    m_page = m_totalPages = 1;
}

bool CandidateWindow::IsVisible() const {
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void CandidateWindow::Paint(HWND hwnd) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    RECT bounds;
    GetClientRect(hwnd, &bounds);
    const Palette& colors = AppsUseDarkMode() ? kDark : kLight;
    Fill(dc, bounds, colors.surface);
    SetBkMode(dc, TRANSPARENT);

    HFONT titleFont = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT itemFont = CreateFontW(-19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei UI");
    HFONT smallFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT original = static_cast<HFONT>(SelectObject(dc, titleFont));

    RECT header = { PADDING + 6, 2, WIN_W - 90, HEADER_H - 1 };
    SetTextColor(dc, colors.ink);
    DrawTextW(dc, m_composition.c_str(), -1, &header,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, smallFont);
    RECT mode = { WIN_W - 88, 2, WIN_W - PADDING - 5, HEADER_H - 1 };
    SetTextColor(dc, colors.muted);
    const wchar_t* status = m_loading ? L"更新中" :
        m_offline ? (m_cached ? L"離線快取" : L"離線") :
        m_cached ? L"快取" : L"粵拼";
    DrawTextW(dc, status, -1, &mode,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
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
            if (!m_loading && i == 0) Fill(dc, row, colors.selected);
            else if (!m_loading && i == m_hoverRow) Fill(dc, row, colors.hover);
            if (!m_loading && i == 0) {
                RECT accent = { 1, top + 5, 4, top + ITEM_H - 5 };
                Fill(dc, accent, colors.accent);
            }
            RECT number = { PADDING + 6, top, PADDING + 34, top + ITEM_H };
            SelectObject(dc, smallFont);
            SetTextColor(dc, m_loading ? colors.muted :
                (i == 0 ? colors.accent : colors.muted));
            std::wstring digit = std::to_wstring(i + 1) + L".";
            DrawTextW(dc, digit.c_str(), -1, &number, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(dc, itemFont);
            SetTextColor(dc, m_loading ? colors.muted : colors.ink);
            RECT candidate = { PADDING + 35, top, WIN_W - PADDING - 4, top + ITEM_H };
            DrawTextW(dc, m_candidates[i].text.c_str(), -1, &candidate,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (!m_candidates[i].annotation.empty()) {
                SIZE wordSize = {};
                GetTextExtentPoint32W(dc, m_candidates[i].text.c_str(),
                    static_cast<int>(m_candidates[i].text.size()), &wordSize);
                int noteLeft = candidate.left + wordSize.cx + 7;
                if (noteLeft < WIN_W - 42) {
                    SelectObject(dc, smallFont);
                    SetTextColor(dc, colors.muted);
                    RECT note = { noteLeft, top, WIN_W - PADDING - 4, top + ITEM_H };
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
    SelectObject(dc, smallFont);
    SetTextColor(dc, !m_loading && m_totalPages > 1 ? colors.accent : colors.muted);
    RECT previous = { PADDING, footerTop + 1, 48, bounds.bottom - 1 };
    RECT next = { WIN_W - 48, footerTop + 1, WIN_W - PADDING, bounds.bottom - 1 };
    DrawTextW(dc, L"‹", -1, &previous, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(dc, L"›", -1, &next, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    wchar_t pageText[48];
    if (m_loading) wcscpy_s(pageText, L"正在搜尋…");
    else swprintf_s(pageText, L"%d / %d", m_page, m_totalPages);
    RECT page = { 48, footerTop + 1, WIN_W - 48, bounds.bottom - 1 };
    SetTextColor(dc, colors.muted);
    DrawTextW(dc, pageText, -1, &page, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    HPEN border = CreatePen(PS_SOLID, 1, colors.line);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, border));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    Rectangle(dc, 0, 0, bounds.right, bounds.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(border);
    SelectObject(dc, original);
    DeleteObject(titleFont);
    DeleteObject(itemFont);
    DeleteObject(smallFont);
    EndPaint(hwnd, &paint);
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
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        if (self) {
            int y = GET_Y_LPARAM(lp);
            int row = (y - HEADER_H) / ITEM_H;
            if (self->m_loading || y < HEADER_H || row >= static_cast<int>(self->m_candidates.size())) row = -1;
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
        if (self) {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int row = (y - HEADER_H) / ITEM_H;
            if (y >= HEADER_H && y < HEADER_H + ITEM_H * static_cast<int>(self->m_candidates.size())
                && row >= 0 && row < static_cast<int>(self->m_candidates.size())
                && !self->m_loading && self->m_cb) {
                self->m_cb(row);
            } else if (y >= HEADER_H + ITEM_H * (std::max)(1, static_cast<int>(self->m_candidates.size()))
                && !self->m_loading && self->m_totalPages > 1 && self->m_pageCb) {
                if (x < 48) self->m_pageCb(-1);
                else if (x >= WIN_W - 48) self->m_pageCb(1);
            }
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
