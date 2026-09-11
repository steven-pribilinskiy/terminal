// GitHub credentials follow the same CLI-first order for settings and previews.
#pragma once
#include "ProcessCaptureImpl.h"
#include <string>
#include <algorithm>
#include <set>
#include <vector>
#include <cwctype>
namespace Microsoft::Terminal
{
    inline bool ValidGitHubOwner(std::wstring_view value)
    {
        if (value.empty() || value.size() > 39 || value.front() == L'-' || value.back() == L'-') return false;
        return std::all_of(value.begin(), value.end(), [](wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-'; });
    }
    inline std::vector<std::wstring> GitHubOwners(std::wstring_view text)
    {
        std::vector<std::wstring> result;
        std::set<std::wstring> seen;
        std::wstring owner;
        const auto add = [&] {
            auto lower = owner; std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
            if (ValidGitHubOwner(owner) && seen.insert(lower).second) result.push_back(owner);
            owner.clear();
        };
        for (const auto ch : text) { if (ch == L',' || iswspace(ch)) add(); else owner += ch; } add();
        return result;
    }
    inline std::wstring GitHubToken(const std::wstring& fallback)
    {
        const auto output = TerminalUtils::CaptureProcess(L"cmd.exe /d /c gh auth token --hostname github.com 2>nul", {}, 4000);
        auto token = til::u8u16(output);
        while (!token.empty() && (token.back() == L'\r' || token.back() == L'\n' || token.back() == L' ')) token.pop_back();
        const auto valid = !token.empty() && std::all_of(token.begin(), token.end(), [](wchar_t ch) { return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'_'; });
        return valid ? token : fallback;
    }
}
