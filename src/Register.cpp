#include "common.h"
#include <string>

// Defined in dllmain.cpp
extern HINSTANCE g_hInst;

// ============================================================
// Registry helpers
// ============================================================
namespace {

struct ComApartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    bool usable() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
};

HRESULT SetRegValue(HKEY hKey, LPCWSTR name, LPCWSTR value) {
    DWORD len = (DWORD)((wcslen(value) + 1) * sizeof(wchar_t));
    LONG r = RegSetValueExW(hKey, name, 0, REG_SZ, (const BYTE*)value, len);
    return HRESULT_FROM_WIN32(r);
}

// Get the full path of this DLL at runtime
std::wstring GetDllPath() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_hInst, path, MAX_PATH);
    return path;
}

// CLSID string from GUID
std::wstring GuidToString(REFGUID guid) {
    wchar_t buf[40];
    StringFromGUID2(guid, buf, 40);
    return buf;
}

} // namespace

// ============================================================
// DllRegisterServer / DllUnregisterServer
// ============================================================

STDAPI DllRegisterServer() {
    ComApartment com;
    if (!com.usable()) return com.result;

    std::wstring clsidStr = GuidToString(CLSID_YutpingIME);
    std::wstring dllPath  = GetDllPath();

    // 1. Register CLSID under HKLM\Software\Classes\CLSID\{...}
    std::wstring clsidKey = L"Software\\Classes\\CLSID\\" + clsidStr;
    HKEY hKey = nullptr;
    LONG r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, clsidKey.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    SetRegValue(hKey, nullptr, L"Yutping IME (粵拼)");
    RegCloseKey(hKey);

    // InprocServer32
    std::wstring inprocKey = clsidKey + L"\\InprocServer32";
    r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, inprocKey.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    SetRegValue(hKey, nullptr, dllPath.c_str());
    SetRegValue(hKey, L"ThreadingModel", L"Apartment");
    RegCloseKey(hKey);

    // 2. Register as TSF text service via ITfInputProcessorProfiles
    ITfInputProcessorProfiles* pProfiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles, (void**)&pProfiles);
    if (FAILED(hr)) return hr;

    hr = pProfiles->Register(CLSID_YutpingIME);
    if (SUCCEEDED(hr)) {
        // Register a language profile for Traditional Chinese
        hr = pProfiles->AddLanguageProfile(
            CLSID_YutpingIME,
            YUTPING_LANGID,
            GUID_PROFILE_YUTPING,
            L"粵拼輸入法 (Yutping IME)",
            (ULONG)wcslen(L"粵拼輸入法 (Yutping IME)"),
            dllPath.c_str(),      // icon file (reuse DLL itself)
            (ULONG)dllPath.size(),
            0);                    // icon index
    }
    pProfiles->Release();
    if (FAILED(hr)) return hr;

    // 3. Register TSF categories — Windows Settings "Add a keyboard"
    //    only enumerates TIPs that register GUID_TFCAT_TIP_KEYBOARD.
    //    Without these the profile is invisible in Settings and disappears
    //    after a restart.
    ITfCategoryMgr* pCat = nullptr;
    hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ITfCategoryMgr, (void**)&pCat);
    if (FAILED(hr) || !pCat) return hr ? hr : E_FAIL;

    // Third param = guidItem (the item to associate with this category)
    hr = pCat->RegisterCategory(CLSID_YutpingIME,
        GUID_TFCAT_TIP_KEYBOARD, CLSID_YutpingIME);
    pCat->Release();
    return hr;
}

STDAPI DllUnregisterServer() {
    ComApartment com;
    if (!com.usable()) return com.result;
    std::wstring clsidStr = GuidToString(CLSID_YutpingIME);

    // Remove CLSID registry entries
    std::wstring clsidKey = L"Software\\Classes\\CLSID\\" + clsidStr;
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsidKey.c_str());

    // Remove TSF categories first (must UnregisterCategory before Unregister)
    ITfCategoryMgr* pCat = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITfCategoryMgr, (void**)&pCat);
    if (SUCCEEDED(hr) && pCat) {
        pCat->UnregisterCategory(CLSID_YutpingIME,
            GUID_TFCAT_TIP_KEYBOARD, CLSID_YutpingIME);
        pCat->Release();
    }

    // Remove TSF registration
    ITfInputProcessorProfiles* pProfiles = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles, (void**)&pProfiles);
    if (FAILED(hr)) return hr;

    pProfiles->RemoveLanguageProfile(
        CLSID_YutpingIME, YUTPING_LANGID, GUID_PROFILE_YUTPING);
    pProfiles->Unregister(CLSID_YutpingIME);
    pProfiles->Release();
    return S_OK;
}
