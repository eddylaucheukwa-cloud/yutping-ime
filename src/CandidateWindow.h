#pragma once
#include "common.h"
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
              int page, int totalPages, bool loading, bool cached, bool offline);
    void Hide();

    bool IsVisible() const;

    // Called when user presses a digit key — owner handles commit
    void SetSelectCallback(SelectCallback cb) { m_cb = cb; }
    void SetPageCallback(PageCallback cb) { m_pageCb = cb; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void Paint(HWND hwnd);

    HWND m_hwnd = nullptr;
    std::vector<Candidate> m_candidates;
    std::wstring m_composition;
    int m_page = 1;
    int m_totalPages = 1;
    int m_hoverRow = -1;
    bool m_loading = false;
    bool m_cached = false;
    bool m_offline = false;
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
