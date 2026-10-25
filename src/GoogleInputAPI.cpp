#include "GoogleInputAPI.h"
#include <winhttp.h>
#include <sstream>
#include <codecvt>
#include <locale>
#include <cwchar>
#include <map>
#include <mutex>

#pragma comment(lib, "winhttp.lib")

namespace {
constexpr wchar_t kCacheKey[] = L"Software\\YutpingIME\\CacheV2";
constexpr unsigned long long kCacheLifetime = 7ULL * 24 * 60 * 60 * 10000000;

// WinHTTP pools connections within a session. Workers each own their request,
// while the session lives until DLL unload (workers hold g_cDllRef).
struct HttpSession {
    HINTERNET handle = WinHttpOpen(L"YutpingIME/1.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HttpSession() {
        if (!handle) return;
        WinHttpSetTimeouts(handle, 3000, 3000, 3000, 3000);
        DWORD protocols = WINHTTP_PROTOCOL_FLAG_HTTP2;
        WinHttpSetOption(handle, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &protocols, sizeof(protocols));
    }
    ~HttpSession() { if (handle) WinHttpCloseHandle(handle); }
};

HttpSession& Session() { static HttpSession session; return session; }

std::mutex flightsMutex;
std::map<std::wstring, std::vector<CandidateCallback>> flights;
void FinishQuery(const std::wstring& spelling, CandidateResult result) {
    std::vector<CandidateCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(flightsMutex);
        const auto found = flights.find(spelling);
        if (found == flights.end()) return;
        callbacks = std::move(found->second);
        flights.erase(found);
    }
    for (auto& callback : callbacks) callback(result);
}

unsigned long long FileTimeNow() {
    FILETIME time;
    GetSystemTimeAsFileTime(&time);
    return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) |
        time.dwLowDateTime;
}

bool ReadCache(const std::wstring& spelling, std::vector<Candidate>& candidates,
               bool& fresh) {
    if (spelling.size() > 32) return false;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kCacheKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, bytes = 0;
    LONG result = RegQueryValueExW(key, spelling.c_str(), nullptr, &type, nullptr, &bytes);
    if (result != ERROR_SUCCESS || type != REG_MULTI_SZ || bytes < 4 || bytes > 65536 || bytes % 2) {
        RegCloseKey(key);
        return false;
    }
    std::vector<wchar_t> data(bytes / sizeof(wchar_t) + 1, L'\0');
    result = RegQueryValueExW(key, spelling.c_str(), nullptr, &type,
        reinterpret_cast<BYTE*>(data.data()), &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) return false;
    std::vector<std::wstring> parts;
    size_t count = bytes / sizeof(wchar_t), pos = 0;
    while (pos < count && data[pos] && parts.size() < 91) {
        size_t end = pos;
        while (end < count && data[end]) ++end;
        if (end == count) return false;
        parts.emplace_back(data.data() + pos, end - pos);
        pos = end + 1;
    }
    if (parts.size() < 4 || (parts.size() - 1) % 3 != 0) return false;
    unsigned long long saved = _wcstoui64(parts[0].c_str(), nullptr, 10);
    unsigned long long now = FileTimeNow();
    fresh = saved && now >= saved && now - saved < kCacheLifetime;
    for (size_t i = 1; i + 2 < parts.size(); i += 3) {
        if (parts[i + 1] == L"\x0001") parts[i + 1].clear();
        wchar_t* end = nullptr;
        auto length = wcstoul(parts[i + 2].c_str(), &end, 10);
        if (!end || end == parts[i + 2].c_str() || *end || length > spelling.size()) {
            candidates.clear();
            return false;
        }
        candidates.push_back({ std::move(parts[i]), std::move(parts[i + 1]), length });
    }
    return !candidates.empty();
}

void WriteCache(const std::wstring& spelling, const std::vector<Candidate>& candidates) {
    if (spelling.empty() || spelling.size() > 32 || candidates.empty()) return;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kCacheKey, 0, nullptr, 0,
            KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    std::vector<wchar_t> data;
    auto append = [&](const std::wstring& value) {
        data.insert(data.end(), value.begin(), value.end());
        data.push_back(L'\0');
    };
    append(std::to_wstring(FileTimeNow()));
    for (const Candidate& candidate : candidates) {
        append(candidate.text);
        append(candidate.annotation.empty() ? L"\x0001" : candidate.annotation);
        append(std::to_wstring(candidate.matchedLength));
    }
    data.push_back(L'\0');
    RegSetValueExW(key, spelling.c_str(), 0, REG_MULTI_SZ,
        reinterpret_cast<const BYTE*>(data.data()),
        static_cast<DWORD>(data.size() * sizeof(wchar_t)));
    DWORD values = 0;
    if (RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, &values, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS && values > 200) {
        wchar_t oldest[64];
        DWORD length = 64;
        DWORD index = 0;
        if (RegEnumValueW(key, index, oldest, &length, nullptr, nullptr, nullptr, nullptr)
                == ERROR_SUCCESS && spelling == oldest) {
            index = 1;
            length = 64;
        }
        if (RegEnumValueW(key, index, oldest, &length, nullptr, nullptr, nullptr, nullptr)
            == ERROR_SUCCESS) RegDeleteValueW(key, oldest);
    }
    RegCloseKey(key);
}

std::vector<std::wstring> ReadStrings(const std::string& json, size_t pos, size_t limit) {
    std::vector<std::wstring> values;
    while (pos < json.size() && json[pos] != ']' && values.size() < limit) {
        if (json[pos] != '"') { ++pos; continue; }
        ++pos;
        std::string utf8;
        while (pos < json.size() && json[pos] != '"') {
            char c = json[pos++];
            if (c == '\\' && pos < json.size()) {
                c = json[pos++];
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
            }
            utf8.push_back(c);
        }
        if (pos >= json.size()) break;
        ++pos;
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (length > 0 || utf8.empty()) {
            std::wstring wide(length, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
            values.push_back(std::move(wide));
        }
    }
    return values;
}
} // namespace

// Keep Google's per-candidate pronunciation annotations with the characters.
std::vector<Candidate> GoogleInputAPI::ParseResponse(const std::string& json) {
    std::vector<Candidate> results;
    size_t entry = json.find(",[[\"");
    if (entry == std::string::npos) return results;
    size_t array = json.find(",[\"", entry + 4);
    if (array == std::string::npos) return results;
    size_t end = json.find(']', array + 2);
    if (end == std::string::npos) return results;
    auto words = ReadStrings(json, array + 2, MAX_CANDIDATES);
    std::vector<std::wstring> notes;
    std::vector<size_t> lengths;
    size_t key = json.find("\"annotation\"", end);
    if (key != std::string::npos) {
        size_t opening = json.find('[', key);
        if (opening != std::string::npos) notes = ReadStrings(json, opening + 1, words.size());
    }
    key = json.find("\"matched_length\"", end);
    if (key != std::string::npos) {
        size_t pos = json.find('[', key);
        if (pos != std::string::npos) {
            ++pos;
            while (pos < json.size() && lengths.size() < words.size()) {
                while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\r' ||
                    json[pos] == '\n' || json[pos] == '\t' || json[pos] == ',')) ++pos;
                if (pos == json.size() || json[pos] < '0' || json[pos] > '9') break;
                size_t value = 0;
                while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
                    // Bound untrusted values rather than permitting integer overflow.
                    value = (std::min)(size_t(65536), value * 10 + json[pos++] - '0');
                }
                lengths.push_back(value);
                if (pos == json.size() || (json[pos] != ',' && json[pos] != ']' &&
                    json[pos] != ' ' && json[pos] != '\r' && json[pos] != '\n' && json[pos] != '\t')) break;
            }
        }
    }
    for (size_t i = 0; i < words.size(); ++i)
        results.push_back({ std::move(words[i]), i < notes.size() ? std::move(notes[i]) : L"",
            i < lengths.size() ? lengths[i] : 0 });
    return results;
}

void GoogleInputAPI::ClearCache() {
    RegDeleteTreeW(HKEY_CURRENT_USER, kCacheKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\YutpingIME\\Cache");
}

std::string GoogleInputAPI::WStringToUtf8(const std::wstring& ws) {
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), s.data(), len, nullptr, nullptr);
    return s;
}

bool GoogleInputAPI::TryCached(const std::wstring& romaji, CandidateResult& result) {
    std::vector<Candidate> saved;
    bool fresh = false;
    if (!ReadCache(romaji, saved, fresh) || !fresh) return false;
    result = { std::move(saved), true, false };
    return true;
}

void GoogleInputAPI::WarmUp() {
    static std::once_flag once;
    std::call_once(once, [] {
        extern LONG g_cDllRef;
        InterlockedIncrement(&g_cDllRef);
        try {
            std::thread([] {
                extern LONG g_cDllRef;
                struct HoldDll { ~HoldDll() { InterlockedDecrement(&g_cDllRef); } } hold;
                auto session = Session().handle;
                if (!session) return;
                auto connection = WinHttpConnect(session, L"inputtools.google.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
                if (!connection) return;
                auto request = WinHttpOpenRequest(connection, L"HEAD", L"/", nullptr,
                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                if (request) {
                    DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
                    WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled));
                    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) WinHttpReceiveResponse(request, nullptr);
                    WinHttpCloseHandle(request);
                }
                WinHttpCloseHandle(connection);
            }).detach();
        } catch (...) { InterlockedDecrement(&g_cDllRef); }
    });
}

void GoogleInputAPI::QueryAsync(const std::wstring& romaji, CandidateCallback cb) {
    if (romaji.empty()) {
        cb({});
        return;
    }

    std::vector<Candidate> saved;
    bool fresh = false;
    bool hasSaved = ReadCache(romaji, saved, fresh);
    if (hasSaved && fresh) {
        cb({ std::move(saved), true, false });
        return;
    }

    {
        std::lock_guard<std::mutex> lock(flightsMutex);
        auto& waiting = flights[romaji];
        waiting.push_back(std::move(cb));
        if (waiting.size() > 1) return;
    }

    extern LONG g_cDllRef;
    InterlockedIncrement(&g_cDllRef);
    try {
    std::thread([romaji, saved, hasSaved]() {
        extern LONG g_cDllRef;
        struct HoldDll { ~HoldDll() { InterlockedDecrement(&g_cDllRef); } } hold;
        auto fail = [&]() { FinishQuery(romaji, { saved, hasSaved, true }); };
        // Build URL path
        // GET /request?text=<romaji>&itc=yue-hant-t-i0-und&num=27&cp=0&cs=1&ie=utf-8&oe=utf-8
        std::string romajiUtf8 = WStringToUtf8(romaji);

        // URL-encode (simple: alphanumeric passthrough, spaces to +)
        std::string encoded;
        for (unsigned char c : romajiUtf8) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded += c;
            } else {
                char buf[4];
                snprintf(buf, sizeof(buf), "%%%02X", c);
                encoded += buf;
            }
        }

        std::string path = "/request?text=" + encoded +
            "&itc=yue-hant-t-i0-und&num=27&cp=0&cs=1&ie=utf-8&oe=utf-8";
        std::wstring wpath(path.begin(), path.end());

        HINTERNET hSession = Session().handle;
        if (!hSession) { fail(); return; }

        HINTERNET hConnect = WinHttpConnect(hSession,
            L"inputtools.google.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) { fail(); return; }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET",
            wpath.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            fail();
            return;
        }

        std::string body;
        bool networkOkay = false;
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, nullptr))
        {
            networkOkay = true;
            DWORD size = 0;
            do {
                if (!WinHttpQueryDataAvailable(hRequest, &size)) {
                    networkOkay = false;
                    break;
                }
                if (size == 0) break;
                std::string buf(size, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(hRequest, &buf[0], size, &read)) {
                    networkOkay = false;
                    break;
                }
                buf.resize(read);
                body += buf;
            } while (size > 0);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);

        if (networkOkay && body.rfind("[\"SUCCESS\"", 0) == 0) {
            auto parsed = ParseResponse(body);
            WriteCache(romaji, parsed);
            FinishQuery(romaji, { std::move(parsed), false, false });
        } else {
            fail();
        }
    }).detach();
    } catch (...) {
        InterlockedDecrement(&g_cDllRef);
        FinishQuery(romaji, { std::move(saved), hasSaved, true });
    }
}
