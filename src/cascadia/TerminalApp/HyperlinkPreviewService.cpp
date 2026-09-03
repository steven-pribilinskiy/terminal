// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "HyperlinkPreviewService.h"
#include "ProcessCapture.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Cryptography.Certificates.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Web.Http.Headers.h>

#include <til/string.h>
#include <til/u8u16convert.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <optional>
#include <set>
#include <utility>
#include <vector>

// Last, and deliberately: <icu.h> brings a large C header full of macros along
// with it, and everything above should see the SDK's own spelling of things.
#include <til/regex.h>

using namespace winrt::Windows::Data::Json;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;

namespace Model = winrt::Microsoft::Terminal::Settings::Model;
namespace Control = winrt::Microsoft::Terminal::Control;
namespace WWH = winrt::Windows::Web::Http;

namespace winrt::TerminalApp::implementation
{
    // Everything the service knows between two Rebuild() calls. Immutable once
    // published, so a fetch that is already running keeps a coherent view of
    // the world even while the user is editing settings.
    struct HyperlinkPreviewSnapshot
    {
        // A compiled matcher. The regex here is a TEMPLATE and is never matched
        // against directly: a URegularExpression carries the match state, so
        // each use clones it (uregex_clone, which is far cheaper than reopening
        // the pattern -- the same reasoning as Terminal::_getPatterns).
        struct Matcher
        {
            Model::IntegrationMatcherKind Kind{ Model::IntegrationMatcherKind::Link };
            til::ICU::unique_uregex Regex;
            // ICU can map a name to a group number but cannot enumerate the
            // names in a pattern, so they are scraped out of the pattern text.
            std::vector<std::wstring> GroupNames;
            std::wstring HostSetting;
            std::wstring LinkTemplate;
        };

        struct Step
        {
            std::wstring Id;
            bool IsCommand{ false };
            std::wstring Url;
            std::wstring Method;
            std::vector<std::pair<std::wstring, std::wstring>> Headers;
            std::wstring AuthType;
            std::wstring AuthUser;
            std::wstring AuthPassword;
            std::wstring Body;
            bool AllowUntrusted{ false };
            std::wstring CommandLine;
            std::wstring Stdin;
            std::wstring When;
            std::wstring Unless;
            unsigned long TimeoutMs{ 8000 };
        };

        struct Field
        {
            std::wstring Key;
            std::wstring Label;
            std::wstring Path;
            std::wstring IconPath;
            std::wstring ColorPath;
            std::wstring Color;
            std::wstring Format;
            int32_t Kind{ 0 };
        };

        struct Plugin
        {
            std::wstring Id;
            std::wstring Name;
            std::wstring Icon;
            std::wstring Html;
            int32_t CacheSeconds{ 300 };
            // Every required setting has a value and every credential is
            // present. Nothing is ever fetched for an integration that isn't.
            bool Configured{ false };
            std::map<std::wstring, std::wstring> Settings;
            std::map<std::wstring, std::wstring> Credentials;
            std::vector<Matcher> Matchers;
            std::vector<Step> Steps;
            // Only the fields the user chose to see, in manifest order.
            std::vector<Field> Fields;
        };

        // The window's enabled text-kind tooltip rules. These decide whether a
        // piece of plain terminal text is eligible for a preview at all.
        struct TextRule
        {
            til::ICU::unique_uregex Regex;
            std::wstring Integration;
        };

        std::vector<std::shared_ptr<Plugin>> Plugins;
        std::vector<TextRule> TextRules;
    };
}

namespace
{
    using Snapshot = winrt::TerminalApp::implementation::HyperlinkPreviewSnapshot;
    using TemplateValueMap = std::map<std::wstring, std::wstring>;

    // ---- small string helpers -------------------------------------------

    std::wstring_view Trim(std::wstring_view text) noexcept
    {
        while (!text.empty() && (text.front() == L' ' || text.front() == L'\t'))
        {
            text.remove_prefix(1);
        }
        while (!text.empty() && (text.back() == L' ' || text.back() == L'\t'))
        {
            text.remove_suffix(1);
        }
        return text;
    }

    // ---- ICU ------------------------------------------------------------

    til::ICU::unique_uregex CompileRegex(const std::wstring& pattern)
    {
        if (pattern.empty())
        {
            return nullptr;
        }
        UErrorCode status = U_ZERO_ERROR;
        auto regex = til::ICU::CreateRegex(pattern, 0, &status);
        if (U_FAILURE(status))
        {
            // An unusable pattern is skipped, never fatal: manifests and rules
            // both come from files a person edits by hand.
            return nullptr;
        }
        return regex;
    }

    // The named capture groups a pattern declares. `(?<=` and `(?<!` are
    // lookbehind, not a group name, and an escaped `\(` opens nothing.
    std::vector<std::wstring> GroupNamesIn(std::wstring_view pattern)
    {
        std::vector<std::wstring> names;
        for (size_t i = 0; i + 3 < pattern.size(); ++i)
        {
            if (pattern[i] != L'(' || pattern[i + 1] != L'?' || pattern[i + 2] != L'<')
            {
                continue;
            }

            size_t backslashes = 0;
            for (size_t j = i; j-- > 0 && pattern[j] == L'\\';)
            {
                ++backslashes;
            }
            if (backslashes % 2 != 0)
            {
                continue;
            }

            if (pattern[i + 3] == L'=' || pattern[i + 3] == L'!')
            {
                continue;
            }

            const auto close = pattern.find(L'>', i + 3);
            if (close == std::wstring_view::npos)
            {
                continue;
            }

            const auto name = pattern.substr(i + 3, close - i - 3);
            const auto usable = !name.empty() && std::all_of(name.begin(), name.end(), [](wchar_t ch) {
                return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'_';
            });
            if (usable)
            {
                names.emplace_back(name);
            }
        }
        return names;
    }

    // Runs one matcher against `text`, filling `groups` with the named captures
    // plus the conventional `match` (the whole match) and `uri` (the input).
    bool TryMatch(const til::ICU::unique_uregex& compiled,
                  const std::vector<std::wstring>& groupNames,
                  const std::wstring& text,
                  bool requireFullMatch,
                  TemplateValueMap& groups)
    {
        if (!compiled || text.empty())
        {
            return false;
        }

        UErrorCode status = U_ZERO_ERROR;
        til::ICU::unique_uregex regex{ uregex_clone(compiled.get(), &status) };
        if (U_FAILURE(status) || !regex)
        {
            return false;
        }

        uregex_setText(regex.get(), reinterpret_cast<const UChar*>(text.data()), gsl::narrow_cast<int32_t>(text.size()), &status);
        if (U_FAILURE(status))
        {
            return false;
        }

        const auto matched = requireFullMatch ? uregex_matches(regex.get(), 0, &status) : uregex_find(regex.get(), 0, &status);
        if (U_FAILURE(status) || !matched)
        {
            return false;
        }

        status = U_ZERO_ERROR;
        const auto wholeStart = uregex_start(regex.get(), 0, &status);
        const auto wholeEnd = uregex_end(regex.get(), 0, &status);
        if (U_SUCCESS(status) && wholeStart >= 0 && wholeEnd >= wholeStart)
        {
            groups[L"match"] = text.substr(gsl::narrow_cast<size_t>(wholeStart), gsl::narrow_cast<size_t>(wholeEnd - wholeStart));
        }
        groups[L"uri"] = text;

        for (const auto& name : groupNames)
        {
            status = U_ZERO_ERROR;
            const auto number = uregex_groupNumberFromName(regex.get(),
                                                           reinterpret_cast<const UChar*>(name.data()),
                                                           gsl::narrow_cast<int32_t>(name.size()),
                                                           &status);
            if (U_FAILURE(status) || number <= 0)
            {
                continue;
            }

            status = U_ZERO_ERROR;
            const auto start = uregex_start(regex.get(), number, &status);
            const auto end = uregex_end(regex.get(), number, &status);
            if (U_FAILURE(status) || start < 0 || end < start)
            {
                continue;
            }
            groups[name] = text.substr(gsl::narrow_cast<size_t>(start), gsl::narrow_cast<size_t>(end - start));
        }
        return true;
    }

    // ---- JSON -----------------------------------------------------------

    std::wstring FormatNumber(double value)
    {
        if (std::isfinite(value) && value == std::floor(value) && std::fabs(value) < 1e15)
        {
            return fmt::format(L"{}", static_cast<int64_t>(value));
        }
        return fmt::format(L"{}", value);
    }

    std::wstring ValueToString(const IJsonValue& value)
    {
        if (!value)
        {
            return {};
        }
        switch (value.ValueType())
        {
        case JsonValueType::String:
            return std::wstring{ value.GetString() };
        case JsonValueType::Number:
            return FormatNumber(value.GetNumber());
        case JsonValueType::Boolean:
            return value.GetBoolean() ? std::wstring{ L"true" } : std::wstring{ L"false" };
        default:
            // Null, and whole objects or arrays, have no sensible one-line
            // rendering; an empty field is simply not shown.
            return {};
        }
    }

    // RFC 6901 token escapes, decoded left to right so that "~01" is "~1".
    std::wstring UnescapePointerToken(std::wstring_view token)
    {
        std::wstring out;
        out.reserve(token.size());
        for (size_t i = 0; i < token.size(); ++i)
        {
            if (token[i] == L'~' && i + 1 < token.size())
            {
                if (token[i + 1] == L'1')
                {
                    out.push_back(L'/');
                    ++i;
                    continue;
                }
                if (token[i + 1] == L'0')
                {
                    out.push_back(L'~');
                    ++i;
                    continue;
                }
            }
            out.push_back(token[i]);
        }
        return out;
    }

    // RFC 6901, plus one extension: a negative array index counts back from the
    // end, so Slack's "the message we asked for" is /messages/-1/text whether
    // the API returned it alone or with context around it.
    IJsonValue ResolvePointer(const IJsonValue& root, std::wstring_view pointer)
    {
        if (!root)
        {
            return nullptr;
        }
        if (pointer.empty())
        {
            return root;
        }
        if (pointer.front() != L'/')
        {
            return nullptr;
        }

        auto current = root;
        size_t pos = 1;
        while (pos <= pointer.size())
        {
            if (!current)
            {
                return nullptr;
            }

            auto next = pointer.find(L'/', pos);
            if (next == std::wstring_view::npos)
            {
                next = pointer.size();
            }
            const auto token = UnescapePointerToken(pointer.substr(pos, next - pos));

            switch (current.ValueType())
            {
            case JsonValueType::Object:
            {
                const auto object = current.GetObject();
                const winrt::hstring key{ token };
                if (!object.HasKey(key))
                {
                    return nullptr;
                }
                current = object.Lookup(key);
                break;
            }
            case JsonValueType::Array:
            {
                const auto array = current.GetArray();
                int64_t index = 0;
                try
                {
                    size_t consumed = 0;
                    index = std::stoll(token, &consumed);
                    if (consumed != token.size())
                    {
                        return nullptr;
                    }
                }
                catch (...)
                {
                    return nullptr;
                }

                const auto size = static_cast<int64_t>(array.Size());
                if (index < 0)
                {
                    index += size;
                }
                if (index < 0 || index >= size)
                {
                    return nullptr;
                }
                current = array.GetAt(static_cast<uint32_t>(index));
                break;
            }
            default:
                return nullptr;
            }

            pos = next + 1;
        }
        return current;
    }

    // ---- templates ------------------------------------------------------

    struct ExpandContext
    {
        const TemplateValueMap* Groups{ nullptr };
        const TemplateValueMap* Settings{ nullptr };
        const TemplateValueMap* Credentials{ nullptr };
        const std::map<std::wstring, IJsonValue>* Results{ nullptr };
    };

    // `fromData` says whether the value came from the matched text or from an
    // earlier step's result, as opposed to from the user's own configuration.
    // Only the former is percent-encoded inside a URL: a setting like
    // "https://stith.lvh.me" is a whole scheme and host and must stay as typed.
    std::wstring LookupToken(std::wstring_view name, const ExpandContext& context, bool& fromData)
    {
        fromData = false;

        constexpr std::wstring_view settingsPrefix{ L"settings." };
        constexpr std::wstring_view credentialsPrefix{ L"credentials." };

        const auto lookIn = [](const TemplateValueMap* map, std::wstring_view key) -> std::wstring {
            if (!map)
            {
                return {};
            }
            const auto found = map->find(std::wstring{ key });
            return found == map->end() ? std::wstring{} : found->second;
        };

        if (name.starts_with(settingsPrefix))
        {
            return lookIn(context.Settings, name.substr(settingsPrefix.size()));
        }
        if (name.starts_with(credentialsPrefix))
        {
            return lookIn(context.Credentials, name.substr(credentialsPrefix.size()));
        }

        fromData = true;

        // "<stepId>:<json-pointer>" reaches into an earlier step's result.
        if (const auto colon = name.find(L':'); colon != std::wstring_view::npos)
        {
            if (context.Results)
            {
                const auto found = context.Results->find(std::wstring{ name.substr(0, colon) });
                if (found != context.Results->end())
                {
                    return ValueToString(ResolvePointer(found->second, name.substr(colon + 1)));
                }
            }
            return {};
        }

        return lookIn(context.Groups, name);
    }

    // {{name}} substitution, and nothing else. An unknown name expands to
    // nothing, which is what makes `when`/`unless` work as presence tests.
    std::wstring Expand(std::wstring_view templateText, const ExpandContext& context, bool urlEncode)
    {
        std::wstring out;
        out.reserve(templateText.size());

        size_t pos = 0;
        while (pos < templateText.size())
        {
            const auto open = templateText.find(L"{{", pos);
            if (open == std::wstring_view::npos)
            {
                out.append(templateText.substr(pos));
                break;
            }
            out.append(templateText.substr(pos, open - pos));

            const auto close = templateText.find(L"}}", open + 2);
            if (close == std::wstring_view::npos)
            {
                // An unterminated placeholder is literal text, not an error.
                out.append(templateText.substr(open));
                break;
            }

            const auto name = Trim(templateText.substr(open + 2, close - open - 2));
            auto fromData = false;
            auto value = LookupToken(name, context, fromData);
            if (urlEncode && fromData && !value.empty())
            {
                value = std::wstring{ Uri::EscapeComponent(winrt::hstring{ value }) };
            }
            out.append(value);

            pos = close + 2;
        }
        return out;
    }

    // ---- time -----------------------------------------------------------

    // Days since 1970-01-01 for a proleptic Gregorian date. Howard Hinnant's
    // days_from_civil; written out because std::chrono::year_month_day is a
    // C++20 feature this project does not otherwise lean on, and because
    // std::chrono::from_stream is not usable for the offset forms below.
    int64_t DaysFromCivil(int64_t year, int64_t month, int64_t day) noexcept
    {
        year -= month <= 2 ? 1 : 0;
        const auto era = (year >= 0 ? year : year - 399) / 400;
        const auto yearOfEra = year - era * 400; // [0, 399]
        const auto dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1; // [0, 365]
        const auto dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear; // [0, 146096]
        return era * 146097 + dayOfEra - 719468;
    }

    // Accepts the two shapes the built-in manifests actually produce:
    // Jira/stith's ISO 8601 ("2026-09-03T10:11:12.345+0000", "...Z",
    // "...+02:00") and Slack's fractional Unix seconds ("1725360000.123456").
    std::optional<int64_t> ParseEpochSeconds(const std::wstring& text)
    {
        auto year = 0;
        auto month = 0;
        auto day = 0;
        auto hour = 0;
        auto minute = 0;
        auto second = 0;

        if (text.size() >= 19 &&
            swscanf_s(text.c_str(), L"%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second) == 6)
        {
            int64_t offsetSeconds = 0;
            // Scanned from index 19 so the date's own '-' separators cannot be
            // mistaken for a negative UTC offset.
            for (size_t i = 19; i < text.size(); ++i)
            {
                const auto ch = text[i];
                if (ch == L'Z' || ch == L'z')
                {
                    break;
                }
                if (ch == L'+' || ch == L'-')
                {
                    auto offsetHours = 0;
                    auto offsetMinutes = 0;
                    const auto rest = text.substr(i + 1);
                    if (swscanf_s(rest.c_str(), L"%2d:%2d", &offsetHours, &offsetMinutes) == 2 ||
                        swscanf_s(rest.c_str(), L"%2d%2d", &offsetHours, &offsetMinutes) == 2)
                    {
                        offsetSeconds = (offsetHours * 3600LL + offsetMinutes * 60LL) * (ch == L'-' ? -1 : 1);
                    }
                    break;
                }
            }

            if (month < 1 || month > 12 || day < 1 || day > 31)
            {
                return std::nullopt;
            }
            return DaysFromCivil(year, month, day) * 86400LL + hour * 3600LL + minute * 60LL + second - offsetSeconds;
        }

        wchar_t* end = nullptr;
        const auto seconds = std::wcstod(text.c_str(), &end);
        if (end != text.c_str() && seconds > 0.0 && std::isfinite(seconds))
        {
            return static_cast<int64_t>(seconds);
        }
        return std::nullopt;
    }

    std::wstring RelativeTime(const std::wstring& text)
    {
        const auto epoch = ParseEpochSeconds(text);
        if (!epoch)
        {
            // Unparseable: show what the service actually said rather than
            // silently dropping the field.
            return text;
        }

        const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const auto diff = now - *epoch;

        // A timestamp in the future is a clock skew, not a fact worth showing.
        if (diff < 60)
        {
            return L"just now";
        }
        if (diff < 3600)
        {
            return fmt::format(L"{} min ago", diff / 60);
        }
        if (diff < 86400)
        {
            return fmt::format(L"{} h ago", diff / 3600);
        }
        if (diff < 7 * 86400)
        {
            return fmt::format(L"{} d ago", diff / 86400);
        }
        if (diff < 30 * 86400)
        {
            return fmt::format(L"{} wk ago", diff / (7 * 86400));
        }
        if (diff < 365 * 86400)
        {
            return fmt::format(L"{} mo ago", diff / (30 * 86400));
        }
        return fmt::format(L"{} y ago", diff / (365 * 86400));
    }

    std::wstring LocalDate(const std::wstring& text)
    {
        const auto epoch = ParseEpochSeconds(text);
        if (!epoch || *epoch < 0)
        {
            return text;
        }

        // FILETIME counts 100ns ticks from 1601-01-01.
        const auto ticks = (static_cast<uint64_t>(*epoch) + 11644473600ULL) * 10000000ULL;
        FILETIME fileTime{ static_cast<DWORD>(ticks & 0xffffffffULL), static_cast<DWORD>(ticks >> 32) };

        SYSTEMTIME utc{};
        SYSTEMTIME local{};
        if (!FileTimeToSystemTime(&fileTime, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        {
            return text;
        }
        return fmt::format(L"{:04}-{:02}-{:02} {:02}:{:02}", local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute);
    }

    // ---- hosts ----------------------------------------------------------

    std::wstring UriHost(const std::wstring& text)
    {
        try
        {
            const Uri uri{ winrt::hstring{ text } };
            return std::wstring{ uri.Host() };
        }
        catch (...)
        {
            // Not a URI at all -- a text match, or something malformed.
            return {};
        }
    }

    // A hostSetting's value may be spelled as a bare host ("acme.atlassian.net")
    // or as a whole URL ("https://stith.lvh.me"). Both name the same host, and
    // the guard has to agree with either, or it silently stops matching.
    std::wstring HostOfSetting(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }
        if (const auto host = UriHost(value); !host.empty())
        {
            return host;
        }

        auto host = value;
        if (const auto scheme = host.find(L"://"); scheme != std::wstring::npos)
        {
            host.erase(0, scheme + 3);
        }
        if (const auto slash = host.find_first_of(L"/?#"); slash != std::wstring::npos)
        {
            host.erase(slash);
        }
        if (const auto at = host.find(L'@'); at != std::wstring::npos)
        {
            host.erase(0, at + 1);
        }
        if (const auto colon = host.find(L':'); colon != std::wstring::npos)
        {
            host.erase(colon);
        }
        return host;
    }

    // ---- matching -------------------------------------------------------

    struct MatchResult
    {
        std::shared_ptr<Snapshot::Plugin> Owner;
        const Snapshot::Matcher* Matcher{ nullptr };
        TemplateValueMap Groups;
    };

    std::optional<MatchResult> MatchIn(const std::vector<std::shared_ptr<Snapshot::Plugin>>& plugins,
                                       Model::IntegrationMatcherKind kind,
                                       const std::wstring& text,
                                       bool requireFullMatch)
    {
        for (const auto& plugin : plugins)
        {
            for (const auto& matcher : plugin->Matchers)
            {
                if (matcher.Kind != kind)
                {
                    continue;
                }

                TemplateValueMap groups;
                if (!TryMatch(matcher.Regex, matcher.GroupNames, text, requireFullMatch, groups))
                {
                    continue;
                }

                // The host guard. Without it, a look-alike link would be enough
                // to make us send this integration's credentials somewhere it
                // has never heard of.
                if (!matcher.HostSetting.empty())
                {
                    const auto host = UriHost(text);
                    if (host.empty())
                    {
                        continue;
                    }
                    const auto setting = plugin->Settings.find(matcher.HostSetting);
                    if (setting == plugin->Settings.end())
                    {
                        continue;
                    }
                    const auto expected = HostOfSetting(setting->second);
                    if (expected.empty() || !til::equals_insensitive_ascii(host, expected))
                    {
                        continue;
                    }
                }

                return MatchResult{ plugin, &matcher, std::move(groups) };
            }
        }
        return std::nullopt;
    }

    // Which integration, if any, owns this hovered text.
    //
    // hint is what the matching tooltip rule asked for: "" picks automatically,
    // "none" refuses, anything else names a single integration.
    std::optional<MatchResult> FindMatch(const std::shared_ptr<const Snapshot>& snapshot,
                                         const std::wstring& text,
                                         const std::wstring& hint,
                                         bool textMatchersOnly)
    {
        if (!snapshot || text.empty() || hint == L"none")
        {
            return std::nullopt;
        }

        std::vector<std::shared_ptr<Snapshot::Plugin>> candidates;
        for (const auto& plugin : snapshot->Plugins)
        {
            if (!hint.empty() && plugin->Id != hint)
            {
                continue;
            }
            candidates.push_back(plugin);
        }
        if (candidates.empty())
        {
            return std::nullopt;
        }

        // A text match has to be claimed by a rule first: the rules are what
        // made this text hoverable, and they say which integration owns it.
        for (const auto& rule : snapshot->TextRules)
        {
            TemplateValueMap ruleGroups;
            if (!TryMatch(rule.Regex, {}, text, true, ruleGroups))
            {
                continue;
            }
            if (rule.Integration == L"none")
            {
                continue;
            }

            std::vector<std::shared_ptr<Snapshot::Plugin>> pool;
            if (rule.Integration.empty())
            {
                pool = candidates;
            }
            else
            {
                for (const auto& plugin : candidates)
                {
                    if (plugin->Id == rule.Integration)
                    {
                        pool.push_back(plugin);
                    }
                }
            }

            if (auto found = MatchIn(pool, Model::IntegrationMatcherKind::Text, text, true))
            {
                return found;
            }
        }

        if (textMatchersOnly)
        {
            return std::nullopt;
        }
        return MatchIn(candidates, Model::IntegrationMatcherKind::Link, text, false);
    }

    // ---- fetching -------------------------------------------------------

    struct StepOutcome
    {
        std::wstring Body;
        std::wstring Error;
    };

    WWH::HttpClient MakeClient(bool allowUntrustedCertificate)
    {
        WWH::Filters::HttpBaseProtocolFilter filter{};
        if (allowUntrustedCertificate)
        {
            // Only what the manifest asked for. A self-signed development cert
            // is Untrusted; a name mismatch is a different error and stays fatal.
            filter.IgnorableServerCertificateErrors().Append(winrt::Windows::Security::Cryptography::Certificates::ChainValidationResult::Untrusted);
        }

        WWH::HttpClient client{ filter };
        client.DefaultRequestHeaders().UserAgent().TryParseAdd(L"WindowsTerminal");
        return client;
    }

    std::wstring BasicAuthToken(const std::wstring& user, const std::wstring& password)
    {
        using namespace winrt::Windows::Security::Cryptography;
        const auto joined = user + L":" + password;
        const auto buffer = CryptographicBuffer::ConvertStringToBinary(winrt::hstring{ joined }, BinaryStringEncoding::Utf8);
        return std::wstring{ CryptographicBuffer::EncodeToBase64String(buffer) };
    }

    StepOutcome RunHttpStep(const Snapshot::Step& step,
                            const ExpandContext& context,
                            const std::wstring& integrationName,
                            WWH::HttpClient& plainClient,
                            WWH::HttpClient& lenientClient)
    {
        StepOutcome outcome;

        const auto url = Expand(step.Url, context, true);
        if (url.empty())
        {
            outcome.Error = fmt::format(L"{}: the step has no URL", integrationName);
            return outcome;
        }

        auto& client = step.AllowUntrusted ? lenientClient : plainClient;
        if (!client)
        {
            client = MakeClient(step.AllowUntrusted);
        }

        const WWH::HttpMethod method{ winrt::hstring{ step.Method.empty() ? std::wstring{ L"GET" } : step.Method } };
        const WWH::HttpRequestMessage request{ method, Uri{ winrt::hstring{ url } } };

        for (const auto& header : step.Headers)
        {
            if (header.first.empty())
            {
                continue;
            }
            request.Headers().TryAppendWithoutValidation(winrt::hstring{ header.first },
                                                         winrt::hstring{ Expand(header.second, context, false) });
        }

        if (step.AuthType == L"basic")
        {
            const auto user = Expand(step.AuthUser, context, false);
            const auto password = Expand(step.AuthPassword, context, false);
            request.Headers().Authorization(WWH::Headers::HttpCredentialsHeaderValue{ L"Basic", winrt::hstring{ BasicAuthToken(user, password) } });
        }
        else if (step.AuthType == L"bearer")
        {
            request.Headers().Authorization(WWH::Headers::HttpCredentialsHeaderValue{ L"Bearer", winrt::hstring{ Expand(step.AuthPassword, context, false) } });
        }
        else if (step.AuthType == L"header" && !step.AuthUser.empty())
        {
            request.Headers().TryAppendWithoutValidation(winrt::hstring{ step.AuthUser },
                                                         winrt::hstring{ Expand(step.AuthPassword, context, false) });
        }

        if (!step.Body.empty())
        {
            request.Content(WWH::HttpStringContent{ winrt::hstring{ Expand(step.Body, context, false) },
                                                    winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8,
                                                    L"application/json" });
        }

        // HttpClient has no timeout of its own, so the deadline is imposed here.
        // (RemoteIconCache has none at all; don't copy that.)
        const std::chrono::milliseconds timeout{ step.TimeoutMs };

        auto send = client.SendRequestAsync(request);
        if (send.wait_for(timeout) == AsyncStatus::Started)
        {
            send.Cancel();
            outcome.Error = fmt::format(L"{}: timed out", integrationName);
            return outcome;
        }
        // Rethrows whatever the request failed with; the caller turns it into
        // the card's error line.
        const auto response = send.GetResults();

        auto read = response.Content().ReadAsStringAsync();
        if (read.wait_for(timeout) == AsyncStatus::Started)
        {
            read.Cancel();
            outcome.Error = fmt::format(L"{}: timed out", integrationName);
            return outcome;
        }
        outcome.Body = std::wstring{ read.GetResults() };

        if (!response.IsSuccessStatusCode())
        {
            // The body is still handed back: an API that answers 4xx with a
            // JSON explanation should get to say what went wrong.
            outcome.Error = fmt::format(L"{}: {} {}",
                                        integrationName,
                                        static_cast<uint32_t>(response.StatusCode()),
                                        std::wstring{ response.ReasonPhrase() });
        }
        return outcome;
    }

    StepOutcome RunCommandStep(const Snapshot::Step& step, const ExpandContext& context, const std::wstring& integrationName)
    {
        StepOutcome outcome;

        auto commandLine = Expand(step.CommandLine, context, false);
        if (commandLine.empty())
        {
            outcome.Error = fmt::format(L"{}: the step has no command", integrationName);
            return outcome;
        }

        const auto input = til::u16u8(Expand(step.Stdin, context, false));
        const auto output = ::TerminalApp::RunProcessCapture(std::move(commandLine), input, step.TimeoutMs);
        if (output.empty())
        {
            outcome.Error = fmt::format(L"{}: the command produced no output", integrationName);
            return outcome;
        }

        outcome.Body = til::u8u16(output);
        return outcome;
    }

    // Runs the whole pipeline for one integration and renders the result.
    // Blocking; always called from a background thread.
    Control::HyperlinkPreview RunFetch(const Snapshot::Plugin& plugin, const TemplateValueMap& groups)
    {
        Control::HyperlinkPreview preview{};
        preview.IntegrationId(winrt::hstring{ plugin.Id });
        preview.IntegrationName(winrt::hstring{ plugin.Name });
        preview.IntegrationIcon(winrt::hstring{ plugin.Icon });

        auto fields = winrt::single_threaded_vector<Control::HyperlinkPreviewField>();
        preview.Fields(fields);

        std::map<std::wstring, IJsonValue> results;
        IJsonValue last{ nullptr };
        std::wstring error;

        ExpandContext context;
        context.Groups = &groups;
        context.Settings = &plugin.Settings;
        context.Credentials = &plugin.Credentials;
        context.Results = &results;

        WWH::HttpClient plainClient{ nullptr };
        WWH::HttpClient lenientClient{ nullptr };

        for (const auto& step : plugin.Steps)
        {
            if (!step.When.empty() && Expand(step.When, context, false).empty())
            {
                continue;
            }
            if (!step.Unless.empty() && !Expand(step.Unless, context, false).empty())
            {
                continue;
            }

            StepOutcome outcome;
            try
            {
                outcome = step.IsCommand ? RunCommandStep(step, context, plugin.Name) :
                                           RunHttpStep(step, context, plugin.Name, plainClient, lenientClient);
            }
            catch (const winrt::hresult_error& e)
            {
                outcome.Error = fmt::format(L"{}: {}", plugin.Name, std::wstring{ e.message() });
            }
            catch (...)
            {
                outcome.Error = fmt::format(L"{}: the request failed", plugin.Name);
            }

            // Parsed even when the step reported an error: Slack answers 200
            // with {"ok":false,"error":...}, and a 4xx body often carries the
            // only useful detail there is.
            if (!outcome.Body.empty())
            {
                JsonObject asObject{ nullptr };
                JsonArray asArray{ nullptr };
                IJsonValue parsed{ nullptr };
                if (JsonObject::TryParse(winrt::hstring{ outcome.Body }, asObject))
                {
                    parsed = asObject;
                }
                else if (JsonArray::TryParse(winrt::hstring{ outcome.Body }, asArray))
                {
                    parsed = asArray;
                }

                if (parsed)
                {
                    if (!step.Id.empty())
                    {
                        results[step.Id] = parsed;
                    }
                    last = parsed;
                }
            }

            if (!outcome.Error.empty())
            {
                error = outcome.Error;
                break;
            }
        }

        // An unqualified path reads the last step that produced anything;
        // "stepId:/pointer" names one explicitly.
        const auto lookup = [&](const std::wstring& path) -> std::wstring {
            if (path.empty())
            {
                return {};
            }
            if (path.front() != L'/')
            {
                const auto colon = path.find(L':');
                if (colon == std::wstring::npos)
                {
                    return {};
                }
                const auto found = results.find(path.substr(0, colon));
                if (found == results.end())
                {
                    return {};
                }
                return ValueToString(ResolvePointer(found->second, std::wstring_view{ path }.substr(colon + 1)));
            }
            return ValueToString(ResolvePointer(last, path));
        };

        for (const auto& field : plugin.Fields)
        {
            auto value = lookup(field.Path);
            if (field.Format == L"relativeTime")
            {
                value = RelativeTime(value);
            }
            else if (field.Format == L"date")
            {
                value = LocalDate(value);
            }
            if (value.empty())
            {
                continue;
            }

            Control::HyperlinkPreviewField row{};
            row.Label(winrt::hstring{ field.Label });
            row.Value(winrt::hstring{ value });
            // The two enums are declared in the same order on purpose.
            row.Kind(static_cast<Control::HyperlinkPreviewFieldKind>(field.Kind));
            if (!field.IconPath.empty())
            {
                row.IconUri(winrt::hstring{ lookup(field.IconPath) });
            }
            auto color = field.ColorPath.empty() ? std::wstring{} : lookup(field.ColorPath);
            if (color.empty())
            {
                color = field.Color;
            }
            row.Color(winrt::hstring{ color });
            fields.Append(row);
        }

        if (!plugin.Html.empty())
        {
            // Handed over untouched: what to do with it is the control's call.
            preview.Html(winrt::hstring{ plugin.Html });
        }
        if (!error.empty())
        {
            preview.Error(winrt::hstring{ error });
        }
        return preview;
    }
}

namespace winrt::TerminalApp::implementation
{
    void HyperlinkPreviewService::Rebuild(const Model::CascadiaSettings& settings, const Model::WindowSettings& windowSettings)
    try
    {
        auto snapshot = std::make_shared<HyperlinkPreviewSnapshot>();

        IMap<hstring, Model::IntegrationSettings> configured{ nullptr };
        IVector<Model::IntegrationManifest> manifests{ nullptr };
        if (settings)
        {
            if (const auto globals = settings.GlobalSettings())
            {
                configured = globals.Integrations();
            }
        }
        if (configured)
        {
            manifests = Model::IntegrationRegistry::All();
        }

        if (manifests)
        {
            for (const auto& manifest : manifests)
            {
                if (!manifest)
                {
                    continue;
                }

                const auto id = manifest.Id();
                if (id.empty() || !configured.HasKey(id))
                {
                    continue;
                }
                const auto entry = configured.Lookup(id);
                if (!entry || !entry.Enabled())
                {
                    continue;
                }

                auto plugin = std::make_shared<HyperlinkPreviewSnapshot::Plugin>();
                plugin->Id = std::wstring{ id };
                plugin->Name = std::wstring{ manifest.Name() };
                plugin->Icon = std::wstring{ manifest.Icon() };
                plugin->Html = std::wstring{ manifest.Html() };
                plugin->CacheSeconds = manifest.CacheSeconds();

                if (const auto values = entry.Values())
                {
                    for (const auto& pair : values)
                    {
                        plugin->Settings[std::wstring{ pair.Key() }] = std::wstring{ pair.Value() };
                    }
                }

                auto configuredFully = true;
                if (const auto settingFields = manifest.Settings())
                {
                    for (const auto& field : settingFields)
                    {
                        if (!field || !field.Required())
                        {
                            continue;
                        }
                        const auto found = plugin->Settings.find(std::wstring{ field.Key() });
                        if (found == plugin->Settings.end() || found->second.empty())
                        {
                            configuredFully = false;
                        }
                    }
                }

                // The ONLY place the credential vault is read. A hover must
                // never reach it: it is slow, it is audited, and it would put a
                // vault call on the path of every mouse move over a link.
                if (const auto credentialFields = manifest.Credentials())
                {
                    for (const auto& field : credentialFields)
                    {
                        if (!field || field.Key().empty())
                        {
                            continue;
                        }
                        std::wstring value;
                        try
                        {
                            value = std::wstring{ Model::IntegrationCredentialStore::Get(id, field.Key()) };
                        }
                        catch (...)
                        {
                            // Nothing stored, or the vault refused: not configured.
                        }
                        if (value.empty())
                        {
                            configuredFully = false;
                        }
                        plugin->Credentials[std::wstring{ field.Key() }] = std::move(value);
                    }
                }
                plugin->Configured = configuredFully;

                if (const auto matchers = manifest.Matchers())
                {
                    for (const auto& matcher : matchers)
                    {
                        if (!matcher)
                        {
                            continue;
                        }
                        const std::wstring pattern{ matcher.Pattern() };
                        auto compiled = CompileRegex(pattern);
                        if (!compiled)
                        {
                            continue;
                        }

                        HyperlinkPreviewSnapshot::Matcher entryMatcher;
                        entryMatcher.Kind = matcher.Kind();
                        entryMatcher.Regex = std::move(compiled);
                        entryMatcher.GroupNames = GroupNamesIn(pattern);
                        entryMatcher.HostSetting = std::wstring{ matcher.HostSetting() };
                        entryMatcher.LinkTemplate = std::wstring{ matcher.LinkTemplate() };
                        plugin->Matchers.push_back(std::move(entryMatcher));
                    }
                }

                if (const auto steps = manifest.FetchSteps())
                {
                    for (const auto& step : steps)
                    {
                        if (!step)
                        {
                            continue;
                        }
                        HyperlinkPreviewSnapshot::Step entryStep;
                        entryStep.Id = std::wstring{ step.Id() };
                        entryStep.IsCommand = step.Type() == Model::IntegrationFetchType::Command;
                        entryStep.Url = std::wstring{ step.Url() };
                        entryStep.Method = std::wstring{ step.Method() };
                        entryStep.AuthType = std::wstring{ step.AuthType() };
                        entryStep.AuthUser = std::wstring{ step.AuthUser() };
                        entryStep.AuthPassword = std::wstring{ step.AuthPassword() };
                        entryStep.Body = std::wstring{ step.Body() };
                        entryStep.AllowUntrusted = step.AllowUntrustedCertificate();
                        entryStep.CommandLine = std::wstring{ step.CommandLine() };
                        entryStep.Stdin = std::wstring{ step.Stdin() };
                        entryStep.When = std::wstring{ step.When() };
                        entryStep.Unless = std::wstring{ step.Unless() };
                        entryStep.TimeoutMs = step.TimeoutMs() > 0 ? static_cast<unsigned long>(step.TimeoutMs()) : 8000ul;

                        if (const auto headers = step.Headers())
                        {
                            for (const auto& pair : headers)
                            {
                                entryStep.Headers.emplace_back(std::wstring{ pair.Key() }, std::wstring{ pair.Value() });
                            }
                        }
                        plugin->Steps.push_back(std::move(entryStep));
                    }
                }

                // A null Fields collection means "the manifest's own defaults";
                // an empty one means the user unticked everything.
                const auto chosen = entry.Fields();
                std::set<std::wstring> selected;
                if (chosen)
                {
                    for (const auto& key : chosen)
                    {
                        selected.insert(std::wstring{ key });
                    }
                }

                if (const auto displayFields = manifest.Fields())
                {
                    for (const auto& field : displayFields)
                    {
                        if (!field)
                        {
                            continue;
                        }
                        const std::wstring key{ field.Key() };
                        const auto visible = chosen ? selected.count(key) != 0 : field.DefaultVisible();
                        if (!visible)
                        {
                            continue;
                        }

                        HyperlinkPreviewSnapshot::Field entryField;
                        entryField.Key = key;
                        entryField.Label = std::wstring{ field.Label() };
                        entryField.Path = std::wstring{ field.Path() };
                        entryField.IconPath = std::wstring{ field.IconPath() };
                        entryField.ColorPath = std::wstring{ field.ColorPath() };
                        entryField.Color = std::wstring{ field.Color() };
                        entryField.Format = std::wstring{ field.Format() };
                        entryField.Kind = static_cast<int32_t>(field.Kind());
                        plugin->Fields.push_back(std::move(entryField));
                    }
                }

                snapshot->Plugins.push_back(std::move(plugin));
            }
        }

        if (windowSettings)
        {
            if (const auto rules = windowSettings.HyperlinkTooltipRules())
            {
                for (const auto& rule : rules)
                {
                    if (!rule || !rule.Enabled() || rule.Kind() != Model::HyperlinkMatchKind::Text)
                    {
                        continue;
                    }
                    auto compiled = CompileRegex(std::wstring{ rule.Pattern() });
                    if (!compiled)
                    {
                        continue;
                    }

                    HyperlinkPreviewSnapshot::TextRule textRule;
                    textRule.Regex = std::move(compiled);
                    textRule.Integration = std::wstring{ rule.Integration() };
                    snapshot->TextRules.push_back(std::move(textRule));
                }
            }
        }

        std::scoped_lock lock{ _mutex };
        _snapshot = std::move(snapshot);
        // Whatever was cached was rendered against the old configuration.
        _cache.clear();
    }
    CATCH_LOG()

    std::shared_ptr<const HyperlinkPreviewSnapshot> HyperlinkPreviewService::_currentSnapshot() const
    {
        std::scoped_lock lock{ _mutex };
        return _snapshot;
    }

    Control::HyperlinkPreview HyperlinkPreviewService::_cacheLookup(const std::wstring& key)
    {
        std::scoped_lock lock{ _mutex };
        const auto found = _cache.find(key);
        if (found == _cache.end())
        {
            return nullptr;
        }
        if (std::chrono::steady_clock::now() >= found->second.Expiry)
        {
            _cache.erase(found);
            return nullptr;
        }
        return found->second.Preview;
    }

    void HyperlinkPreviewService::_cacheStore(const std::wstring& key, const Control::HyperlinkPreview& preview, int32_t seconds)
    {
        std::scoped_lock lock{ _mutex };
        // Bounded: this lives for the life of the window, and a long session
        // can hover an unbounded number of distinct links.
        if (_cache.size() >= 256)
        {
            _cache.clear();
        }
        _cache[key] = CacheEntry{ preview, std::chrono::steady_clock::now() + std::chrono::seconds{ seconds } };
    }

    // Text match -> the URL it stands for. A real URI is already its own link
    // and is left alone, so only text matchers are consulted here.
    hstring HyperlinkPreviewService::ResolveLink(const hstring& text)
    try
    {
        const std::wstring value{ text };
        const auto found = FindMatch(_currentSnapshot(), value, {}, true);
        if (!found || !found->Matcher || found->Matcher->LinkTemplate.empty())
        {
            return {};
        }

        ExpandContext context;
        context.Groups = &found->Groups;
        context.Settings = &found->Owner->Settings;
        context.Credentials = &found->Owner->Credentials;
        return hstring{ Expand(found->Matcher->LinkTemplate, context, false) };
    }
    catch (...)
    {
        LOG_CAUGHT_EXCEPTION();
        return {};
    }

    bool HyperlinkPreviewService::CanPreview(const hstring& text, const hstring& integrationHint)
    try
    {
        const auto found = FindMatch(_currentSnapshot(), std::wstring{ text }, std::wstring{ integrationHint }, false);
        return found && found->Owner->Configured && !found->Owner->Steps.empty();
    }
    catch (...)
    {
        LOG_CAUGHT_EXCEPTION();
        return false;
    }

    IAsyncOperation<Control::HyperlinkPreview> HyperlinkPreviewService::GetPreviewAsync(hstring text, hstring integrationHint)
    {
        auto strongThis = get_strong();

        // Resolved here, on the caller's thread, and then copied out: the
        // snapshot may be replaced by a settings reload while this request is
        // in flight, and the plugin this fetch belongs to must not change
        // underneath it.
        std::shared_ptr<HyperlinkPreviewSnapshot::Plugin> plugin;
        TemplateValueMap groups;
        std::wstring cacheKey;
        Control::HyperlinkPreview cached{ nullptr };

        try
        {
            const std::wstring value{ text };
            if (auto found = FindMatch(strongThis->_currentSnapshot(), value, std::wstring{ integrationHint }, false);
                found && found->Owner->Configured && !found->Owner->Steps.empty())
            {
                plugin = found->Owner;
                groups = std::move(found->Groups);
                cacheKey = plugin->Id + L"|" + value;
                cached = strongThis->_cacheLookup(cacheKey);
            }
        }
        catch (...)
        {
            LOG_CAUGHT_EXCEPTION();
        }

        if (!plugin)
        {
            co_return nullptr;
        }
        if (cached)
        {
            co_return cached;
        }

        co_await winrt::resume_background();

        // Nothing below may throw: a hover is not a place to lose an exception,
        // and the card would rather show why than show nothing.
        const auto failed = [&](const std::wstring& message) {
            Control::HyperlinkPreview broken{};
            broken.IntegrationId(hstring{ plugin->Id });
            broken.IntegrationName(hstring{ plugin->Name });
            broken.IntegrationIcon(hstring{ plugin->Icon });
            broken.Fields(winrt::single_threaded_vector<Control::HyperlinkPreviewField>());
            broken.Error(hstring{ message });
            return broken;
        };

        Control::HyperlinkPreview preview{ nullptr };
        try
        {
            preview = RunFetch(*plugin, groups);
        }
        catch (const winrt::hresult_error& e)
        {
            preview = failed(fmt::format(L"{}: {}", plugin->Name, std::wstring{ e.message() }));
        }
        catch (...)
        {
            preview = failed(fmt::format(L"{}: the preview could not be built", plugin->Name));
        }

        // A failure is cached too, briefly: a wrong token should not mean a
        // fresh round trip on every hover, but it should recover quickly once
        // the token is fixed.
        const auto seconds = preview.Error().empty() ? std::max(1, plugin->CacheSeconds) : 30;
        strongThis->_cacheStore(cacheKey, preview, seconds);

        co_return preview;
    }
}
