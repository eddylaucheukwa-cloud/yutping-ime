#include "SettingsWindow.h"
#include "UserSettings.h"
#include "CandidateWindow.h"
#include <commctrl.h>

namespace {
enum : int { PageSize = 101, Punctuation, Learning, Shortcut, Save, Cancel,
             ClearCache, ClearHistory, Theme, Preview };

HWND Control(HWND parent, const wchar_t* type, const wchar_t* label, DWORD style,
             int x, int y, int width, int height, int id = 0) {
    HWND window = CreateWindowExW(0, type, label,
        WS_CHILD | WS_VISIBLE | (id ? WS_TABSTOP : 0) | style,
        x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(window, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return window;
}

HWND Child(HWND parent, int id) { return GetDlgItem(parent, id); }

CandidateTheme SelectedTheme(HWND window) {
    HWND combo = Child(window, Theme);
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    const LRESULT value = index == CB_ERR ? CB_ERR : SendMessageW(combo, CB_GETITEMDATA, index, 0);
    return value == CB_ERR ? CandidateTheme::System : static_cast<CandidateTheme>(value);
}

void PreviewTheme(HWND window, CandidateWindow& preview) {
    preview.SetTheme(SelectedTheme(window));
    RECT bounds;
    GetWindowRect(window, &bounds);
    MONITORINFO monitor = {sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
    POINT point = { bounds.right + 12, bounds.top + 75 };
    if (point.x + preview.PreferredSize().cx > monitor.rcWork.right) point.x = bounds.left - preview.PreferredSize().cx - 12;
    preview.Show(L"neiho", {{L"你好", L"nei hou", 5}, {L"你可", L"nei ho", 5},
        {L"您好", L"nei hou", 5}, {L"妳好", L"nei hou", 5},
        {L"你", L"nei", 3}, {L"好", L"hou", 2}}, point, 1, 3, false, false, false);
}

LRESULT CALLBACK SettingsProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    static CandidateWindow preview;
    switch (message) {
    case WM_CREATE: {
        CandidateWindow::RegisterClass(GetModuleHandleW(nullptr));
        UserSettings settings = LoadUserSettings();
        Control(window, L"STATIC", L"候選字每頁顯示", 0, 24, 24, 180, 22);
        HWND page = Control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
            220, 20, 145, 140, PageSize);
        for (const wchar_t* label : {L"4 個", L"6 個", L"9 個"})
            SendMessageW(page, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(page, CB_SETCURSEL, settings.pageSize == 4 ? 0 : settings.pageSize == 9 ? 2 : 1, 0);

        HWND punctuation = Control(window, L"BUTTON", L"中文模式使用中文標點", BS_AUTOCHECKBOX,
            24, 69, 340, 25, Punctuation);
        SendMessageW(punctuation, BM_SETCHECK, settings.chinesePunctuation ? BST_CHECKED : BST_UNCHECKED, 0);
        HWND learning = Control(window, L"BUTTON", L"優先顯示常用候選字", BS_AUTOCHECKBOX,
            24, 107, 340, 25, Learning);
        SendMessageW(learning, BM_SETCHECK, settings.learnChoices ? BST_CHECKED : BST_UNCHECKED, 0);

        Control(window, L"STATIC", L"中／英文切換鍵", 0, 24, 153, 180, 22);
        HWND shortcut = Control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
            220, 149, 145, 100, Shortcut);
        SendMessageW(shortcut, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ctrl + Shift"));
        SendMessageW(shortcut, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"F12"));
        SendMessageW(shortcut, CB_SETCURSEL, settings.f12Toggle ? 1 : 0, 0);

        Control(window, L"STATIC", L"選字窗主題", 0, 24, 199, 180, 22);
        HWND theme = Control(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
            166, 195, 199, 230, Theme);
        const struct { const wchar_t* label; CandidateTheme value; } themes[] = {
            {L"System (match Windows)", CandidateTheme::System}, {L"Terminal", CandidateTheme::Terminal},
            {L"Game Boy", CandidateTheme::GameBoy},
            {L"Lucy · Afterglow", CandidateTheme::Lucy}, {L"Lucy · Flow", CandidateTheme::LucyFlow},
            {L"David & Lucy · Earthrise", CandidateTheme::DavidLucy},
            {L"David & Lucy · Embrace", CandidateTheme::DavidLucyEmbrace},
            {L"David & Lucy · Blue Marble", CandidateTheme::Earth},
            {L"Capri · Redline", CandidateTheme::Capri}
        };
        for (const auto& option : themes) {
            const LRESULT index = SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option.label));
            SendMessageW(theme, CB_SETITEMDATA, index, static_cast<LPARAM>(option.value));
            if (option.value == settings.theme) SendMessageW(theme, CB_SETCURSEL, index, 0);
        }
        Control(window, L"BUTTON", L"預覽主題", BS_PUSHBUTTON, 24, 238, 340, 30, Preview);

        Control(window, L"BUTTON", L"清除候選快取", BS_PUSHBUTTON,
            24, 286, 162, 32, ClearCache);
        Control(window, L"BUTTON", L"清除常用字紀錄", BS_PUSHBUTTON,
            202, 286, 162, 32, ClearHistory);
        Control(window, L"STATIC", L"設定儲存後，新輸入會立即套用。", 0,
            24, 330, 340, 23);
        Control(window, L"BUTTON", L"取消", BS_PUSHBUTTON,
            202, 375, 78, 32, Cancel);
        Control(window, L"BUTTON", L"儲存", BS_DEFPUSHBUTTON,
            287, 375, 78, 32, Save);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case Save: {
            UserSettings settings;
            int selected = static_cast<int>(SendMessageW(Child(window, PageSize), CB_GETCURSEL, 0, 0));
            settings.pageSize = selected == 0 ? 4 : selected == 2 ? 9 : 6;
            settings.chinesePunctuation = SendMessageW(Child(window, Punctuation), BM_GETCHECK, 0, 0) == BST_CHECKED;
            settings.learnChoices = SendMessageW(Child(window, Learning), BM_GETCHECK, 0, 0) == BST_CHECKED;
            settings.f12Toggle = SendMessageW(Child(window, Shortcut), CB_GETCURSEL, 0, 0) == 1;
            settings.theme = SelectedTheme(window);
            if (!SaveUserSettings(settings)) {
                MessageBoxW(window, L"無法儲存設定。", L"Yutping IME", MB_OK | MB_ICONERROR);
                return 0;
            }
            DestroyWindow(window);
            return 0;
        }
        case Cancel: DestroyWindow(window); return 0;
        case Preview: PreviewTheme(window, preview); return 0;
        case Theme:
            if (HIWORD(wp) == CBN_SELCHANGE && preview.IsVisible()) PreviewTheme(window, preview);
            return 0;
        case ClearCache:
            ClearCandidateCache();
            MessageBoxW(window, L"候選快取已清除。", L"Yutping IME", MB_OK | MB_ICONINFORMATION);
            return 0;
        case ClearHistory:
            ClearSelectionHistory();
            MessageBoxW(window, L"常用字紀錄已清除。", L"Yutping IME", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: preview.Hide(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
} // namespace

int ShowSettingsWindow(HINSTANCE instance) {
    WNDCLASSEXW cls = { sizeof(cls) };
    cls.lpfnWndProc = SettingsProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.lpszClassName = L"YutpingSettingsWindow";
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"粵拼輸入法設定",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 406, 465, nullptr, nullptr, instance, nullptr);
    if (!window) return 2;
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}
