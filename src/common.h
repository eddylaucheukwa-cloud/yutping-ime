#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <msctf.h>
#include <olectl.h>
#include <string>
#include <vector>
#include <functional>

// {B0F2B76B-8E5B-4A3C-9D1E-7F2A3C4D5E6F}  — POC GUID，正式版要重新生成
// Run:  uuidgen  to replace
DEFINE_GUID(CLSID_YutpingIME,
    0xb0f2b76b, 0x8e5b, 0x4a3c,
    0x9d, 0x1e, 0x7f, 0x2a, 0x3c, 0x4d, 0x5e, 0x6f);

// Language profile GUID
// {C1A2B3C4-D5E6-F7A8-B9C0-D1E2F3A4B5C6}
DEFINE_GUID(GUID_PROFILE_YUTPING,
    0xc1a2b3c4, 0xd5e6, 0xf7a8,
    0xb9, 0xc0, 0xd1, 0xe2, 0xf3, 0xa4, 0xb5, 0xc6);

// Use Hong Kong SAR (zh-HK, 0x0C04) to match the user's installed language
constexpr LANGID YUTPING_LANGID = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_HONGKONG);

// Parser cap — must be >= the num= value in the API request
constexpr int MAX_CANDIDATES = 30;

// How many candidates to show per page in the popup
constexpr int CANDS_PER_PAGE = 6;

struct Candidate {
    std::wstring text;
    std::wstring annotation;
};
