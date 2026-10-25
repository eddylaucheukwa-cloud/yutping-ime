#include "GoogleInputAPI.h"
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>

LONG g_cDllRef = 0;
static std::mutex sendMutex;
static std::condition_variable sendChanged;
static int sends = 0;
static bool releaseSend = false;

// WinHttpConnect/OpenRequest do not send data. Intercept the send boundary so
// this check neither contacts Google nor depends on network timing.
static BOOL WINAPI BlockedSend(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR) {
    std::unique_lock<std::mutex> lock(sendMutex);
    ++sends;
    sendChanged.notify_all();
    sendChanged.wait(lock, [] { return releaseSend; });
    SetLastError(ERROR_WINHTTP_CANNOT_CONNECT);
    return FALSE;
}
#define WinHttpSendRequest BlockedSend
#include "../src/GoogleInputAPI.cpp"
#undef WinHttpSendRequest

int main() {
    const std::wstring spelling = L"__flight_" + std::to_wstring(GetCurrentProcessId());
    std::promise<CandidateResult> results[4];
    std::future<CandidateResult> futures[4];
    for (int i = 0; i < 4; ++i) futures[i] = results[i].get_future();
    GoogleInputAPI::QueryAsync(spelling, [&](CandidateResult result) { results[0].set_value(std::move(result)); });
    {
        std::unique_lock<std::mutex> lock(sendMutex);
        if (!sendChanged.wait_for(lock, std::chrono::seconds(5), [] { return sends == 1; })) return 1;
    }
    for (int i = 1; i < 3; ++i)
        GoogleInputAPI::QueryAsync(spelling, [&, i](CandidateResult result) { results[i].set_value(std::move(result)); });
    {
        std::lock_guard<std::mutex> lock(sendMutex);
        if (sends != 1) return 2;
        releaseSend = true;
    }
    sendChanged.notify_all();
    for (int i = 0; i < 3; ++i) {
        if (futures[i].wait_for(std::chrono::seconds(5)) != std::future_status::ready) return 3;
        if (!futures[i].get().offline) return 4;
    }
    GoogleInputAPI::QueryAsync(spelling, [&](CandidateResult result) { results[3].set_value(std::move(result)); });
    if (futures[3].wait_for(std::chrono::seconds(5)) != std::future_status::ready || !futures[3].get().offline) return 5;
    // A callback finishing does not yet imply that the detached worker exited.
    while (InterlockedCompareExchange(&g_cDllRef, 0, 0)) Sleep(1);
    if (sends != 2) return 6;
    std::cout << "3 callers shared one request; a failed request can be retried\n";
}
