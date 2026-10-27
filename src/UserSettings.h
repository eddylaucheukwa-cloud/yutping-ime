#pragma once
#include "common.h"

// Keep stored IDs stable: 3 was Paper & Ink, 4 was Neon Arcade, 6 was David, 8 was the withdrawn Lucy Flow variant.
enum class CandidateTheme : DWORD { System = 0, Terminal = 1, GameBoy = 2, Lucy = 5, DavidLucy = 7, LucyFlow = 9, DavidLucyEmbrace = 10, Earth = 11, Capri = 12 };

struct UserSettings {
    int pageSize = 6;
    bool chinesePunctuation = true;
    bool learnChoices = true;
    bool f12Toggle = false;
    CandidateTheme theme = CandidateTheme::System;
};

UserSettings LoadUserSettings();
bool SaveUserSettings(const UserSettings& settings);
void RememberSelection(const std::wstring& spelling, const std::wstring& word);
void RankCandidates(const std::wstring& spelling, std::vector<Candidate>& candidates);
void ClearSelectionHistory();
void ClearCandidateCache();
