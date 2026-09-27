#pragma once
#include "common.h"

struct UserSettings {
    int pageSize = 6;
    bool chinesePunctuation = true;
    bool learnChoices = true;
    bool f12Toggle = false;
};

UserSettings LoadUserSettings();
bool SaveUserSettings(const UserSettings& settings);
void RememberSelection(const std::wstring& spelling, const std::wstring& word);
void RankCandidates(const std::wstring& spelling, std::vector<Candidate>& candidates);
void ClearSelectionHistory();
void ClearCandidateCache();
