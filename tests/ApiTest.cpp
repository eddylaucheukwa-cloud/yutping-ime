#include "GoogleInputAPI.h"
#include <string>

LONG g_cDllRef = 0;

int main() {
    std::string response = u8"[\"SUCCESS\",[[\"neihou\",[";
    for (int i = 0; i < 27; ++i) {
        if (i) response += ',';
        response += i == 0 ? u8"\"你好\"" :
            i == 26 ? u8"\"黎\"" : "\"candidate" + std::to_string(i) + "\"";
    }
    response += u8"],[],{\"annotation\":[\"nei hou\",\"nei ho\"]}]] ]";
    auto candidates = GoogleInputAPI::ParseResponse(response);
    if (candidates.size() != 27) return 1;
    if (candidates[0].text != L"你好" || candidates[26].text != L"黎") return 2;
    if (candidates[0].annotation != L"nei hou" || candidates[1].annotation != L"nei ho") return 4;
    if ((candidates.size() + CANDS_PER_PAGE - 1) / CANDS_PER_PAGE != 5) return 3;

    // A fresh local entry must be returned synchronously, without network.
    const std::wstring spelling = L"__yutping_cache_test_" + std::to_wstring(GetCurrentProcessId());
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\YutpingIME\\Cache", 0,
            nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return 5;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    unsigned long long timestamp = (static_cast<unsigned long long>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    std::wstring entry = std::to_wstring(timestamp);
    entry.push_back(L'\0');
    entry += L"你好";
    entry.push_back(L'\0');
    entry += L"nei hou";
    entry.push_back(L'\0');
    entry.push_back(L'\0');
    LONG wrote = RegSetValueExW(key, spelling.c_str(), 0, REG_MULTI_SZ,
        reinterpret_cast<const BYTE*>(entry.data()),
        static_cast<DWORD>(entry.size() * sizeof(wchar_t)));
    RegCloseKey(key);
    if (wrote != ERROR_SUCCESS) return 6;
    bool called = false, correct = false;
    GoogleInputAPI::QueryAsync(spelling, [&](CandidateResult result) {
        called = true;
        correct = result.cached && !result.offline && result.candidates.size() == 1 &&
            result.candidates[0].text == L"你好" && result.candidates[0].annotation == L"nei hou";
    });
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\YutpingIME\\Cache", 0,
            KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, spelling.c_str());
        RegCloseKey(key);
    }
    return called && correct ? 0 : 7;
}
