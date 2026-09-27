#pragma once
#include "common.h"
#include <winhttp.h>
#include <string>
#include <vector>
#include <functional>
#include <thread>

struct CandidateResult {
    std::vector<Candidate> candidates;
    bool cached = false;
    bool offline = false;
};

// Callback may run immediately for a cache hit or on a worker for a request.
using CandidateCallback = std::function<void(CandidateResult)>;

class GoogleInputAPI {
public:
    // The caller owns cancellation and UI-thread dispatch. No worker retains
    // an IME or API object after the host application unloads the DLL.
    static void QueryAsync(const std::wstring& romaji, CandidateCallback cb);
    static std::vector<Candidate> ParseResponse(const std::string& json);
    static void ClearCache();

private:
    static std::string WStringToUtf8(const std::wstring& ws);
};
