#include "UserSettings.h"
#include <algorithm>

namespace {
constexpr wchar_t kSettings[] = L"Software\\YutpingIME\\Settings";
constexpr wchar_t kChoices[] = L"Software\\YutpingIME\\Selections";

DWORD ReadNumber(HKEY key, const wchar_t* name, DWORD fallback) {
    DWORD value = fallback, type = 0, size = sizeof(value);
    if (!key || RegQueryValueExW(key, name, nullptr, &type,
            reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS ||
        type != REG_DWORD || size != sizeof(value)) return fallback;
    return value;
}

bool WriteNumber(HKEY key, const wchar_t* name, DWORD value) {
    return RegSetValueExW(key, name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
}

std::wstring ChoiceKey(const std::wstring& spelling, const std::wstring& word) {
    return spelling + L"|" + word;
}
} // namespace

UserSettings LoadUserSettings() {
    UserSettings settings;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettings, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return settings;
    DWORD size = ReadNumber(key, L"PageSize", 6);
    settings.pageSize = (size == 4 || size == 6 || size == 9) ? static_cast<int>(size) : 6;
    settings.chinesePunctuation = ReadNumber(key, L"ChinesePunctuation", 1) != 0;
    settings.learnChoices = ReadNumber(key, L"LearnChoices", 1) != 0;
    settings.f12Toggle = ReadNumber(key, L"F12Toggle", 0) != 0;
    RegCloseKey(key);
    return settings;
}

bool SaveUserSettings(const UserSettings& settings) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettings, 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    bool okay = WriteNumber(key, L"PageSize", static_cast<DWORD>(settings.pageSize)) &&
        WriteNumber(key, L"ChinesePunctuation", settings.chinesePunctuation ? 1 : 0) &&
        WriteNumber(key, L"LearnChoices", settings.learnChoices ? 1 : 0) &&
        WriteNumber(key, L"F12Toggle", settings.f12Toggle ? 1 : 0);
    RegCloseKey(key);
    return okay;
}

void RememberSelection(const std::wstring& spelling, const std::wstring& word) {
    if (spelling.empty() || word.empty()) return;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kChoices, 0, nullptr, 0,
            KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    std::wstring name = ChoiceKey(spelling, word);
    DWORD count = ReadNumber(key, name.c_str(), 0);
    if (count < 10000) WriteNumber(key, name.c_str(), count + 1);
    RegCloseKey(key);
}

void RankCandidates(const std::wstring& spelling, std::vector<Candidate>& candidates) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kChoices, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return;
    std::vector<DWORD> scores;
    scores.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        std::wstring name = ChoiceKey(spelling, candidate.text);
        scores.push_back(ReadNumber(key, name.c_str(), 0));
    }
    RegCloseKey(key);
    std::vector<size_t> order(candidates.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return scores[a] > scores[b];
    });
    std::vector<Candidate> ranked;
    ranked.reserve(candidates.size());
    for (size_t i : order) ranked.push_back(std::move(candidates[i]));
    candidates = std::move(ranked);
}

void ClearSelectionHistory() {
    RegDeleteTreeW(HKEY_CURRENT_USER, kChoices);
}

void ClearCandidateCache() {
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\YutpingIME\\Cache");
}
