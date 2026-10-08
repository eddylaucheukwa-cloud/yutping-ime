#include "common.h"
#include "SettingsWindow.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <cstdio>
#include <string>

namespace {
using DllRegistration = HRESULT (STDAPICALLTYPE*)();
constexpr wchar_t kUninstallKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\YutpingIME";
constexpr wchar_t kInstalledMessage[] =
    L"Yutping IME 安裝完成。\n\n"
    L"請按 Win + Space，選擇「粵拼輸入法 (Yutping IME)」。\n\n"
    L"已開啟的程式可能仍使用舊版輸入法。請先關閉並重開要輸入的程式。"
    L"若介面仍是舊版，請從 Windows 登出再登入；不需要重裝或重開機。";

bool IsElevated() {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID group = nullptr;
    BOOL member = FALSE;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) {
        CheckTokenMembership(nullptr, group, &member);
        FreeSid(group);
    }
    return member == TRUE;
}

int Elevate(bool uninstall) {
    wchar_t executable[32768];
    if (!GetModuleFileNameW(nullptr, executable, 32768)) return 30;
    SHELLEXECUTEINFOW info = { sizeof(info) };
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = executable;
    info.lpParameters = uninstall ? L"--uninstall-elevated" : L"--install";
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) return 31;
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD result = 32;
    GetExitCodeProcess(info.hProcess, &result);
    CloseHandle(info.hProcess);
    return static_cast<int>(result);
}

int EnableForCurrentUser() {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ITfInputProcessorProfiles* profiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(&profiles));
    if (SUCCEEDED(hr)) {
        hr = profiles->EnableLanguageProfile(CLSID_YutpingIME, YUTPING_LANGID,
            GUID_PROFILE_YUTPING, TRUE);
        profiles->Release();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return SUCCEEDED(hr) ? 0 : 12;
}

int EnsureHongKongLanguage() {
    // Preserve every existing preferred language and add Hong Kong Chinese only
    // when missing. Run in the unelevated user's session after UAC, because
    // UAC may have used a different administrator account.
    wchar_t system[32768];
    if (!GetSystemDirectoryW(system, 32768)) return 40;
    std::wstring powershell = std::wstring(system) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring command = L"\"" + powershell +
        L"\" -NoProfile -NonInteractive -Command \"$ErrorActionPreference='Stop'; "
        L"$list=Get-WinUserLanguageList; "
        L"if (-not (@($list | ForEach-Object {$_.LanguageTag}) -contains 'zh-Hant-HK')) "
        L"{$list.Add('zh-Hant-HK'); Set-WinUserLanguageList -LanguageList $list -Force}\"";
    STARTUPINFOW start = { sizeof(start) };
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr,
            FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &start, &process)) return 41;
    DWORD waited = WaitForSingleObject(process.hProcess, 30000);
    DWORD exitCode = 42;
    if (waited == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return waited == WAIT_OBJECT_0 && exitCode == 0 ? 0 : 42;
}

std::wstring ProgramDirectory() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &path))) return {};
    std::wstring result(path);
    CoTaskMemFree(path);
    return result + L"\\YutpingIME";
}

std::wstring SettingsShortcut() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr, &path))) return {};
    std::wstring result(path);
    CoTaskMemFree(path);
    return result + L"\\粵拼輸入法設定.lnk";
}

bool CreateSettingsShortcut(const std::wstring& executable) {
    std::wstring path = SettingsShortcut();
    if (path.empty()) return false;
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_IShellLinkW, reinterpret_cast<void**>(&link));
    if (FAILED(hr)) return false;
    hr = link->SetPath(executable.c_str());
    if (SUCCEEDED(hr)) hr = link->SetArguments(L"--settings");
    if (SUCCEEDED(hr)) {
        IPersistFile* persist = nullptr;
        hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist));
        if (SUCCEEDED(hr)) {
            hr = persist->Save(path.c_str(), TRUE);
            persist->Release();
        }
    }
    link->Release();
    return SUCCEEDED(hr);
}

std::wstring RegisteredDll() {
    HKEY key = nullptr;
    std::wstring path;
    const wchar_t* name = L"SOFTWARE\\Classes\\CLSID\\{B0F2B76B-8E5B-4A3C-9D1E-7F2A3C4D5E6F}\\InprocServer32";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, name, 0, KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        wchar_t value[32768];
        DWORD size = sizeof(value);
        if (RegQueryValueExW(key, nullptr, nullptr, nullptr,
                reinterpret_cast<BYTE*>(value), &size) == ERROR_SUCCESS) path = value;
        RegCloseKey(key);
    }
    return path;
}

HRESULT CallRegistration(const std::wstring& dll, const char* entry) {
    HMODULE module = LoadLibraryW(dll.c_str());
    if (!module) return HRESULT_FROM_WIN32(GetLastError());
    auto fn = reinterpret_cast<DllRegistration>(GetProcAddress(module, entry));
    HRESULT result = fn ? fn() : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    FreeLibrary(module);
    return result;
}

void SetString(HKEY key, const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

int Install() {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(100), RT_RCDATA);
    if (!resource) return 1;
    HGLOBAL loaded = LoadResource(nullptr, resource);
    const auto* bytes = static_cast<const unsigned char*>(LockResource(loaded));
    DWORD length = SizeofResource(nullptr, resource);
    if (!bytes || length < 2 || bytes[0] != 'M' || bytes[1] != 'Z') return 2;

    // Every distinct build has its own filename so upgrading never overwrites
    // a DLL already loaded by Notepad, Explorer, or another TSF host.
    unsigned long long hash = 14695981039346656037ULL;
    for (DWORD i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * 1099511628211ULL;
    wchar_t version[32];
    swprintf_s(version, L"%016llx", hash);
    std::wstring base = ProgramDirectory();
    if (base.empty()) return 3;
    if (!CreateDirectoryW(base.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return 4;
    std::wstring directory = base + L"\\" + version;
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return 5;
    std::wstring dll = directory + L"\\YutpingIME.dll";

    HANDLE file = CreateFileW(dll.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        bool ok = WriteFile(file, bytes, length, &written, nullptr) && written == length;
        CloseHandle(file);
        if (!ok) { DeleteFileW(dll.c_str()); return 6; }
    } else if (GetLastError() != ERROR_FILE_EXISTS) return 7;

    HRESULT hr = CallRegistration(dll, "DllRegisterServer");
    if (FAILED(hr)) return 10;

    ITfInputProcessorProfiles* profiles = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(&profiles));
    if (FAILED(hr)) return 11;
    // Enable for this user; the default also covers users added later.
    profiles->EnableLanguageProfileByDefault(CLSID_YutpingIME, YUTPING_LANGID,
        GUID_PROFILE_YUTPING, TRUE);
    profiles->Release();
    if (EnableForCurrentUser() != 0) return 12;

    wchar_t exePath[32768];
    if (!GetModuleFileNameW(nullptr, exePath, 32768)) return 13;
    std::wstring uninstaller = base + L"\\Uninstall.exe";
    if (_wcsicmp(exePath, uninstaller.c_str()) != 0 &&
        !CopyFileW(exePath, uninstaller.c_str(), FALSE)) return 14;
    if (!CreateSettingsShortcut(uninstaller)) return 15;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, 0, nullptr, 0,
            KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        SetString(key, L"DisplayName", L"Yutping IME");
        SetString(key, L"DisplayVersion", L"0.1");
        SetString(key, L"UninstallString", L"\"" + uninstaller + L"\" --uninstall");
        SetString(key, L"DisplayIcon", dll);
        RegCloseKey(key);
    }
    return 0;
}

int Uninstall() {
    std::wstring shortcut = SettingsShortcut();
    if (!shortcut.empty()) DeleteFileW(shortcut.c_str());
    std::wstring dll = RegisteredDll();
    if (!dll.empty()) {
        HRESULT hr = CallRegistration(dll, "DllUnregisterServer");
        if (FAILED(hr)) return 20;
        // Loaded TSF DLLs cannot be deleted until all host apps release them.
        MoveFileExW(dll.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kUninstallKey);
    return 0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool verify = argc > 1 && _wcsicmp(argv[1], L"--verify") == 0;
    bool settings = argc > 1 && _wcsicmp(argv[1], L"--settings") == 0;
    bool uninstallChild = argc > 1 && _wcsicmp(argv[1], L"--uninstall-elevated") == 0;
    bool uninstall = uninstallChild || (argc > 1 && _wcsicmp(argv[1], L"--uninstall") == 0);
    bool elevatedChild = uninstallChild || (argc > 1 && _wcsicmp(argv[1], L"--install") == 0);
    LocalFree(argv);
    if (settings) return ShowSettingsWindow(instance);
    if (verify) {
        HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(100), RT_RCDATA);
        if (!resource) return 1;
        const auto* bytes = static_cast<const unsigned char*>(LockResource(LoadResource(nullptr, resource)));
        return bytes && SizeofResource(nullptr, resource) > 1024 && bytes[0] == 'M' && bytes[1] == 'Z' ? 0 : 2;
    }
    if (!IsElevated()) {
        int result = Elevate(uninstall);
        if (result == 0 && !uninstall) result = EnsureHongKongLanguage();
        if (result == 0 && !uninstall) result = EnableForCurrentUser();
        if (result == 0) {
            MessageBoxW(nullptr, uninstall ? L"Yutping IME 已移除。" : kInstalledMessage,
                L"Yutping IME", MB_OK | MB_ICONINFORMATION);
        } else {
            wchar_t message[128];
            swprintf_s(message, L"操作失敗（代碼 %d）。", result);
            MessageBoxW(nullptr, message, L"Yutping IME", MB_OK | MB_ICONERROR);
        }
        return result;
    }
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = uninstall ? Uninstall() : Install();
    if (result == 0 && !uninstall && !elevatedChild) {
        result = EnsureHongKongLanguage();
        if (result == 0) result = EnableForCurrentUser();
    }
    if (SUCCEEDED(com)) CoUninitialize();
    wchar_t message[240];
    if (result == 0 && !elevatedChild) {
        MessageBoxW(nullptr, uninstall ? L"Yutping IME 已移除。" : kInstalledMessage,
            L"Yutping IME", MB_OK | MB_ICONINFORMATION);
    } else if (result != 0 && !elevatedChild) {
        swprintf_s(message, L"操作失敗（代碼 %d）。請確認以管理員權限運行。", result);
        MessageBoxW(nullptr, message, L"Yutping IME", MB_OK | MB_ICONERROR);
    }
    return result;
}
