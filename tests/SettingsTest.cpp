#include "UserSettings.h"

int main() {
    const std::wstring spelling = L"__yutping_test_" + std::to_wstring(GetCurrentProcessId());
    const std::wstring preferred = L"你好";
    const std::wstring original = L"你可";
    RememberSelection(spelling, preferred);
    RememberSelection(spelling, preferred);
    std::vector<Candidate> candidates = {{original, L"first", 3}, {preferred, L"second", 5}};
    RankCandidates(spelling, candidates);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\YutpingIME\\Selections", 0,
            KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, (spelling + L"|" + preferred).c_str());
        RegCloseKey(key);
    }
    return candidates.size() == 2 && candidates[0].text == preferred &&
        candidates[1].text == original && candidates[0].matchedLength == 5 &&
        candidates[1].matchedLength == 3 ? 0 : 1;
}
