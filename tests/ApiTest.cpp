#include "GoogleInputAPI.h"
#include <string>
#include <future>
#include <chrono>
#include <iostream>

LONG g_cDllRef = 0;

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--live") {
        for (const std::wstring spelling : {L"neiho", L"ho", L"neihou", L"neiho"}) {
            auto promise = std::make_shared<std::promise<CandidateResult>>();
            auto future = promise->get_future();
            const auto started = std::chrono::steady_clock::now();
            GoogleInputAPI::QueryAsync(spelling, [promise](CandidateResult result) {
                promise->set_value(std::move(result));
            });
            if (future.wait_for(std::chrono::seconds(15)) != std::future_status::ready) return 10;
            auto result = future.get();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            std::wcout << spelling << L": " << result.candidates.size() << L" candidates, "
                << ms << L" ms, cached=" << result.cached << L", offline=" << result.offline << L'\n';
            if (result.offline || result.candidates.empty()) return 11;
            if (spelling == L"neiho") {
                bool prefix = false;
                for (const auto& candidate : result.candidates)
                    if (candidate.text == L"你" && candidate.matchedLength == 3) prefix = true;
                if (!prefix) return 12;
            }
        }
        return 0;
    }
    std::string response = u8"[\"SUCCESS\",[[\"neihou\",[";
    for (int i = 0; i < 27; ++i) {
        if (i) response += ',';
        response += i == 0 ? u8"\"你好\"" :
            i == 26 ? u8"\"黎\"" : "\"candidate" + std::to_string(i) + "\"";
    }
    response += u8"],[],{\"annotation\":[\"nei hou\",\"nei ho\"],\"matched_length\":[5,3]}]] ]";
    auto candidates = GoogleInputAPI::ParseResponse(response);
    if (candidates.size() != 27) return 1;
    if (candidates[0].text != L"你好" || candidates[26].text != L"黎") return 2;
    if (candidates[0].annotation != L"nei hou" || candidates[1].annotation != L"nei ho") return 4;
    if (candidates[0].matchedLength != 5 || candidates[1].matchedLength != 3 ||
        candidates[2].matchedLength != 0) return 8;
    auto prefix = GoogleInputAPI::ParseResponse(u8"[\"SUCCESS\",[[\"neiho\",[\"你好\",\"你\"],[],{\"annotation\":[\"\",\"nei\"],\"matched_length\":[5,3]}]]]");
    if (prefix.size() != 2 || prefix[1].annotation != L"nei" || prefix[1].matchedLength != 3) return 9;
    if ((candidates.size() + CANDS_PER_PAGE - 1) / CANDS_PER_PAGE != 5) return 3;

    // A fresh local entry must be returned synchronously, without network.
    const std::wstring spelling = L"__yutping_cache_test_" + std::to_wstring(GetCurrentProcessId());
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\YutpingIME\\CacheV2", 0,
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
    entry += L"5";
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
            result.candidates[0].text == L"你好" && result.candidates[0].annotation == L"nei hou" &&
            result.candidates[0].matchedLength == 5;
    });
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\YutpingIME\\CacheV2", 0,
            KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, spelling.c_str());
        RegCloseKey(key);
    }
    return called && correct ? 0 : 7;
}
