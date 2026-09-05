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
#include <map>
#include <mutex>
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
            // A failing optional step is stepped over rather than ending the
            // pipeline: an undocumented endpoint that 400s on some servers, or
            // a probe (`gh auth token`) that is allowed to come back empty.
            bool Optional{ false };
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

        // A display grouping: which field keys belong together, and what to
        // call the heading they sit under.
        struct FieldGroup
        {
            std::wstring Key;
            std::wstring Label;
            std::vector<std::wstring> Fields;
        };

        // Secondary content the user turned on. Body reads one value out of the
        // step results; List repeats one entry shape over an array.
        struct Tab
        {
            std::wstring Key;
            std::wstring Label;
            bool IsList{ false };
            std::wstring Path;
            std::wstring Format;
            std::wstring ItemAuthorPath;
            std::wstring ItemAvatarPath;
            std::wstring ItemBodyPath;
            std::wstring ItemTimePath;
        };

        // Something the user can do to the thing behind the link. A Choice
        // action's options come out of the step results; a Button's request is
        // simply fired.
        struct Action
        {
            std::wstring Key;
            std::wstring Label;
            bool IsChoice{ false };

            std::wstring OptionsPath;
            std::wstring OptionIdPath;
            std::wstring OptionLabelPath;
            std::wstring OptionBadgePath;
            std::wstring OptionColorPath;
            std::wstring OptionTargetIdPath;
            std::wstring CurrentStatePath;
            std::wstring OptionFieldsPath;

            std::wstring Method;
            std::wstring Url;
            std::wstring Body;
            std::vector<std::pair<std::wstring, std::wstring>> Headers;
            std::wstring AuthType;
            std::wstring AuthUser;
            std::wstring AuthPassword;
            bool AllowUntrusted{ false };
            unsigned long TimeoutMs{ 8000 };
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
            // Every group the manifest declared, whether or not any of its
            // fields survived the user's selection -- the stamping below looks
            // a field's group up by key, so an empty group simply never hits.
            std::vector<FieldGroup> FieldGroups;
            // Only the tabs the user chose to see, in manifest order.
            std::vector<Tab> Tabs;
            std::vector<Action> Actions;
            // ICU patterns the terminal scans output for while this integration
            // is enabled. Nothing in the fetch reads them -- what actually puts
            // them in front of the renderer is TerminalSettings, which builds
            // the same list straight off the manifests. They are kept here so
            // the snapshot stays a complete record of what an enabled
            // integration declared.
            std::vector<std::wstring> DetectPatterns;
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
                if (token == L"length")
                {
                    return JsonValue::CreateNumberValue(array.Size());
                }
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
        // {{field.<key>}} -- what the user filled into an action's form. Null
        // everywhere except on the action path.
        const TemplateValueMap* ActionFields{ nullptr };
    };

    // How a substituted value is quoted for the place it lands in.
    enum class Escape
    {
        None,
        Url,
        Json
    };

    // Enough of RFC 8259 to keep a value from breaking out of the string it is
    // being pasted into. Applied to every substitution in a JSON body, not just
    // the data-derived ones: a password with a quote in it would break the body
    // exactly as surely as a comment would.
    std::wstring JsonEscape(std::wstring_view value)
    {
        std::wstring out;
        out.reserve(value.size());
        for (const auto ch : value)
        {
            switch (ch)
            {
            case L'"':
                out.append(L"\\\"");
                break;
            case L'\\':
                out.append(L"\\\\");
                break;
            case L'\b':
                out.append(L"\\b");
                break;
            case L'\f':
                out.append(L"\\f");
                break;
            case L'\n':
                out.append(L"\\n");
                break;
            case L'\r':
                out.append(L"\\r");
                break;
            case L'\t':
                out.append(L"\\t");
                break;
            default:
                if (ch < 0x20)
                {
                    out.append(fmt::format(L"\\u{:04x}", static_cast<uint32_t>(ch)));
                }
                else
                {
                    out.push_back(ch);
                }
                break;
            }
        }
        return out;
    }

    // `fromData` says whether the value came from the matched text or from an
    // earlier step's result, as opposed to from the user's own configuration.
    // Only the former is percent-encoded inside a URL: a setting like
    // "https://stith.lvh.me" is a whole scheme and host and must stay as typed.
    std::wstring LookupToken(std::wstring_view name, const ExpandContext& context, bool& fromData)
    {
        fromData = false;

        constexpr std::wstring_view settingsPrefix{ L"settings." };
        constexpr std::wstring_view credentialsPrefix{ L"credentials." };
        constexpr std::wstring_view fieldPrefix{ L"field." };

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
        if (name.starts_with(fieldPrefix))
        {
            // The user typed it, so it is data even though it never came off
            // the wire -- it gets percent-encoded in a URL like anything else.
            fromData = true;
            return lookIn(context.ActionFields, name.substr(fieldPrefix.size()));
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
    std::wstring Expand(std::wstring_view templateText, const ExpandContext& context, Escape escape)
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
            if (!value.empty())
            {
                if (escape == Escape::Url && fromData)
                {
                    value = std::wstring{ Uri::EscapeComponent(winrt::hstring{ value }) };
                }
                else if (escape == Escape::Json)
                {
                    value = JsonEscape(value);
                }
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
            const auto epochSeconds = seconds > 1e11 ? (seconds / 1000.0) : seconds;
            return static_cast<int64_t>(epochSeconds);
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
            // is Untrusted; local dev certs (e.g. mkcert/lvh.me) also have no
            // revocation servers and incomplete chains, so ignore revocation failures too.
            using namespace winrt::Windows::Security::Cryptography::Certificates;
            filter.IgnorableServerCertificateErrors().Append(ChainValidationResult::Untrusted);
            filter.IgnorableServerCertificateErrors().Append(ChainValidationResult::RevocationInformationMissing);
            filter.IgnorableServerCertificateErrors().Append(ChainValidationResult::RevocationFailure);
            filter.IgnorableServerCertificateErrors().Append(ChainValidationResult::IncompleteChain);
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

    // One HTTP request, described independently of what asked for it: a fetch
    // step and an action's request differ only in where their templates come
    // from, and the auth, the timeout and the error wording must not drift
    // between the two.
    struct HttpCall
    {
        std::wstring_view Url;
        std::wstring_view Method;
        const std::vector<std::pair<std::wstring, std::wstring>>* Headers{ nullptr };
        std::wstring_view AuthType;
        std::wstring_view AuthUser;
        std::wstring_view AuthPassword;
        std::wstring_view Body;
        bool AllowUntrusted{ false };
        unsigned long TimeoutMs{ 8000 };
        // The body is JSON when the manifest wrote one, so substitutions into
        // it are quoted rather than pasted raw.
        Escape BodyEscape{ Escape::None };
        // When set, this exact string is the body and Body is ignored. The
        // action path needs it: it has to expand its template and then edit
        // the resulting JSON before the request goes out.
        const std::wstring* BodyLiteral{ nullptr };
    };

    StepOutcome RunHttpCall(const HttpCall& call,
                            const ExpandContext& context,
                            const std::wstring& integrationName,
                            WWH::HttpClient& plainClient,
                            WWH::HttpClient& lenientClient)
    {
        StepOutcome outcome;

        const auto url = Expand(call.Url, context, Escape::Url);
        if (url.empty())
        {
            outcome.Error = fmt::format(L"{}: the step has no URL", integrationName);
            return outcome;
        }

        auto& client = call.AllowUntrusted ? lenientClient : plainClient;
        if (!client)
        {
            client = MakeClient(call.AllowUntrusted);
        }

        const WWH::HttpMethod method{ winrt::hstring{ call.Method.empty() ? std::wstring_view{ L"GET" } : call.Method } };
        const WWH::HttpRequestMessage request{ method, Uri{ winrt::hstring{ url } } };

        if (call.Headers)
        {
            for (const auto& header : *call.Headers)
            {
                if (header.first.empty())
                {
                    continue;
                }
                request.Headers().TryAppendWithoutValidation(winrt::hstring{ header.first },
                                                             winrt::hstring{ Expand(header.second, context, Escape::None) });
            }
        }

        if (call.AuthType == L"basic")
        {
            const auto user = Expand(call.AuthUser, context, Escape::None);
            const auto password = Expand(call.AuthPassword, context, Escape::None);
            request.Headers().Authorization(WWH::Headers::HttpCredentialsHeaderValue{ L"Basic", winrt::hstring{ BasicAuthToken(user, password) } });
        }
        else if (call.AuthType == L"bearer")
        {
            request.Headers().Authorization(WWH::Headers::HttpCredentialsHeaderValue{ L"Bearer", winrt::hstring{ Expand(call.AuthPassword, context, Escape::None) } });
        }
        else if (call.AuthType == L"header" && !call.AuthUser.empty())
        {
            request.Headers().TryAppendWithoutValidation(winrt::hstring{ call.AuthUser },
                                                         winrt::hstring{ Expand(call.AuthPassword, context, Escape::None) });
        }

        if (call.BodyLiteral ? !call.BodyLiteral->empty() : !call.Body.empty())
        {
            const auto body = call.BodyLiteral ? *call.BodyLiteral : Expand(call.Body, context, call.BodyEscape);
            request.Content(WWH::HttpStringContent{ winrt::hstring{ body },
                                                    winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8,
                                                    L"application/json" });
        }

        // HttpClient has no timeout of its own, so the deadline is imposed here.
        // (RemoteIconCache has none at all; don't copy that.)
        const std::chrono::milliseconds timeout{ call.TimeoutMs };

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

    StepOutcome RunHttpStep(const Snapshot::Step& step,
                            const ExpandContext& context,
                            const std::wstring& integrationName,
                            WWH::HttpClient& plainClient,
                            WWH::HttpClient& lenientClient)
    {
        HttpCall call;
        call.Url = step.Url;
        call.Method = step.Method;
        call.Headers = &step.Headers;
        call.AuthType = step.AuthType;
        call.AuthUser = step.AuthUser;
        call.AuthPassword = step.AuthPassword;
        call.Body = step.Body;
        call.AllowUntrusted = step.AllowUntrusted;
        call.TimeoutMs = step.TimeoutMs;
        // A step's body is authored whole by the manifest and substitutes only
        // matcher groups and settings; the action path is the one that pastes
        // user input in, and it is the one that quotes.
        call.BodyEscape = Escape::None;
        return RunHttpCall(call, context, integrationName, plainClient, lenientClient);
    }

    StepOutcome RunCommandStep(const Snapshot::Step& step, const ExpandContext& context, const std::wstring& integrationName)
    {
        StepOutcome outcome;

        auto commandLine = Expand(step.CommandLine, context, Escape::None);
        if (commandLine.empty())
        {
            outcome.Error = fmt::format(L"{}: the step has no command", integrationName);
            return outcome;
        }

        const auto input = til::u16u8(Expand(step.Stdin, context, Escape::None));
        const auto output = ::TerminalApp::RunProcessCapture(std::move(commandLine), input, step.TimeoutMs);
        if (output.empty())
        {
            outcome.Error = fmt::format(L"{}: the command produced no output", integrationName);
            return outcome;
        }

        outcome.Body = til::u8u16(output);
        return outcome;
    }

    // What a whole pipeline produced.
    struct PipelineOutcome
    {
        // Keyed by step id, exactly as a "stepId:/pointer" path spells it.
        std::map<std::wstring, IJsonValue> Results;
        // The last step that parsed at all: what an unqualified "/pointer"
        // reads against.
        IJsonValue Last{ nullptr };
        std::wstring Error;
        // Optional steps that failed. Deliberately NOT the card's error: an
        // absent `gh` or a Jira site without dev-status is an ordinary outcome,
        // and putting it on a hover card would train the user to ignore the
        // error line. Surfaced only when the fetch produced nothing else.
        std::vector<std::wstring> OptionalErrors;
    };

    // Runs every step of one integration's pipeline. The caller must already
    // have pointed `context.Results` at `outcome.Results`: steps write results
    // into it as they go, and later templates read them back out.
    //
    // Blocking; always called from a background thread.
    void RunPipeline(const Snapshot::Plugin& plugin,
                     const ExpandContext& context,
                     PipelineOutcome& outcome,
                     WWH::HttpClient& plainClient,
                     WWH::HttpClient& lenientClient)
    {
        for (const auto& step : plugin.Steps)
        {
            if (!step.When.empty() && Expand(step.When, context, Escape::None).empty())
            {
                continue;
            }
            if (!step.Unless.empty() && !Expand(step.Unless, context, Escape::None).empty())
            {
                continue;
            }

            StepOutcome stepOutcome;
            try
            {
                stepOutcome = step.IsCommand ? RunCommandStep(step, context, plugin.Name) :
                                               RunHttpStep(step, context, plugin.Name, plainClient, lenientClient);
            }
            catch (const winrt::hresult_error& e)
            {
                stepOutcome.Error = fmt::format(L"{}: {}", plugin.Name, std::wstring{ e.message() });
            }
            catch (...)
            {
                stepOutcome.Error = fmt::format(L"{}: the request failed", plugin.Name);
            }

            // Parsed even when the step reported an error: Slack answers 200
            // with {"ok":false,"error":...}, and a 4xx body often carries the
            // only useful detail there is.
            if (!stepOutcome.Body.empty())
            {
                JsonObject asObject{ nullptr };
                JsonArray asArray{ nullptr };
                IJsonValue parsed{ nullptr };
                if (JsonObject::TryParse(winrt::hstring{ stepOutcome.Body }, asObject))
                {
                    parsed = asObject;
                }
                else if (JsonArray::TryParse(winrt::hstring{ stepOutcome.Body }, asArray))
                {
                    parsed = asArray;
                }

                if (parsed)
                {
                    if (!step.Id.empty())
                    {
                        outcome.Results[step.Id] = parsed;
                    }
                    outcome.Last = parsed;
                }
            }

            if (!stepOutcome.Error.empty())
            {
                if (step.Optional)
                {
                    outcome.OptionalErrors.push_back(std::move(stepOutcome.Error));
                    continue;
                }
                outcome.Error = std::move(stepOutcome.Error);
                break;
            }
        }
    }

    // Reads a path out of a finished pipeline. An unqualified "/pointer" reads
    // the last step that produced anything; "stepId:/pointer" names one.
    //
    // The pointer half may be empty, and "stepId:" then means the step's whole
    // result -- RFC 6901's reading of the empty pointer, which ResolvePointer
    // already implements. That is how a tab reads an endpoint that answers with
    // a bare array rather than an object: GitHub's issue comments are
    // "comments:", with nothing to reach into. A bare "stepId" with no colon
    // at all is accepted as the same thing, since it could not mean anything
    // else here.
    struct ResultReader
    {
        const PipelineOutcome* Pipeline{ nullptr };

        IJsonValue Json(std::wstring_view path) const
        {
            if (!Pipeline || path.empty())
            {
                return nullptr;
            }
            if (path.front() != L'/')
            {
                const auto colon = path.find(L':');
                const auto id = colon == std::wstring_view::npos ? path : path.substr(0, colon);
                const auto pointer = colon == std::wstring_view::npos ? std::wstring_view{} : path.substr(colon + 1);

                const auto found = Pipeline->Results.find(std::wstring{ id });
                if (found == Pipeline->Results.end())
                {
                    return nullptr;
                }
                return ResolvePointer(found->second, pointer);
            }
            return ResolvePointer(Pipeline->Last, path);
        }

        std::wstring Text(std::wstring_view path) const { return ValueToString(Json(path)); }
    };

    // ---- tabs -----------------------------------------------------------

    void AppendText(std::wstring& out, const winrt::hstring& text)
    {
        out.append(text.data(), text.size());
    }

    // A pointer relative to one element, where an empty pointer means "nothing"
    // rather than RFC 6901's "the element itself" -- an unset item path in a
    // manifest is an omission, not a request for the whole object.
    IJsonValue At(const IJsonValue& element, std::wstring_view pointer)
    {
        return pointer.empty() ? nullptr : ResolvePointer(element, pointer);
    }

    void FlattenAdf(const IJsonValue& node, std::wstring& out, int depth);

    void FlattenAdfChildren(const JsonObject& object, std::wstring& out, int depth)
    {
        const auto content = object.GetNamedArray(L"content", nullptr);
        if (!content)
        {
            return;
        }
        for (uint32_t i = 0; i < content.Size(); ++i)
        {
            FlattenAdf(content.GetAt(i), out, depth + 1);
        }
    }

    // Atlassian Document Format -> plain text. ADF is a JSON document tree, not
    // markup, so there is nothing a text renderer could be handed directly.
    // This walks it far enough to read a Jira description or comment and no
    // further: an unrecognised node contributes its children and nothing else,
    // and nothing in here throws or recurses without a bound.
    void FlattenAdf(const IJsonValue& node, std::wstring& out, int depth)
    {
        // Bounded on both axes: hand-written JSON can nest arbitrarily, and a
        // description long enough to matter is already longer than any surface
        // showing it wants to render.
        if (!node || depth > 16 || out.size() > 20000)
        {
            return;
        }
        if (node.ValueType() == JsonValueType::String)
        {
            AppendText(out, node.GetString());
            return;
        }
        if (node.ValueType() != JsonValueType::Object)
        {
            return;
        }

        const auto object = node.GetObject();
        const std::wstring type{ object.GetNamedString(L"type", L"") };

        const auto endLine = [&out]() {
            if (!out.empty() && out.back() != L'\n')
            {
                out.push_back(L'\n');
            }
        };

        if (type == L"text")
        {
            AppendText(out, object.GetNamedString(L"text", L""));
            return;
        }
        if (type == L"hardBreak")
        {
            out.push_back(L'\n');
            return;
        }
        if (type == L"mention" || type == L"emoji")
        {
            if (const auto attrs = object.GetNamedObject(L"attrs", nullptr))
            {
                auto label = attrs.GetNamedString(L"text", L"");
                if (label.empty())
                {
                    label = attrs.GetNamedString(L"shortName", L"");
                }
                AppendText(out, label);
            }
            return;
        }
        if (type == L"rule")
        {
            endLine();
            out.append(L"---\n");
            return;
        }
        if (type == L"codeBlock")
        {
            endLine();
            out.append(L"```\n");
            FlattenAdfChildren(object, out, depth);
            endLine();
            out.append(L"```\n");
            return;
        }
        if (type == L"bulletList" || type == L"orderedList")
        {
            const auto items = object.GetNamedArray(L"content", nullptr);
            if (items)
            {
                const auto ordered = type == L"orderedList";
                for (uint32_t i = 0; i < items.Size(); ++i)
                {
                    endLine();
                    out.append(ordered ? fmt::format(L"{}. ", i + 1) : std::wstring{ L"- " });
                    FlattenAdf(items.GetAt(i), out, depth + 1);
                    endLine();
                }
            }
            return;
        }

        FlattenAdfChildren(object, out, depth);

        // Everything ADF calls a block ends the line it wrote.
        if (type == L"paragraph" || type == L"heading" || type == L"listItem" ||
            type == L"blockquote" || type == L"panel" || type == L"tableRow")
        {
            endLine();
        }
    }

    std::wstring TrimTrailingBlank(std::wstring text)
    {
        while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ' || text.back() == L'\t'))
        {
            text.pop_back();
        }
        return text;
    }

    // The secondary content the user turned on. A tab with nothing in it is
    // dropped rather than shown empty -- an integration that offers Comments
    // should not put a Comments tab on an issue that has none.
    //
    // The kinds are mapped by hand rather than cast: the manifest's Body/List
    // pair has no counterpart for HyperlinkPreviewTabKind::Fields, which names
    // the built-in field list the control always shows first.
    void BuildTabs(const Snapshot::Plugin& plugin, const ResultReader& reader, const Control::HyperlinkPreview& preview)
    {
        auto tabs = winrt::single_threaded_vector<Control::HyperlinkPreviewTab>();

        for (const auto& tab : plugin.Tabs)
        {
            Control::HyperlinkPreviewTab row{};
            row.Key(winrt::hstring{ tab.Key });
            row.Label(winrt::hstring{ tab.Label });

            // ADF is flattened here, so what leaves this function is only ever
            // "text" or "markdown" -- never a format the control would have to
            // know how to parse. The format is stamped on a Comments tab too:
            // GitHub's comment bodies are markdown and Jira's are flattened
            // ADF, and nothing downstream could tell them apart otherwise.
            const auto flatten = tab.Format == L"adf";
            row.Format(winrt::hstring{ flatten || tab.Format.empty() ? std::wstring{ L"text" } : tab.Format });

            if (!tab.IsList)
            {
                row.Kind(Control::HyperlinkPreviewTabKind::Body);

                const auto value = reader.Json(tab.Path);
                std::wstring body;
                if (flatten)
                {
                    FlattenAdf(value, body, 0);
                }
                else
                {
                    body = ValueToString(value);
                }

                body = TrimTrailingBlank(std::move(body));
                if (body.empty())
                {
                    continue;
                }
                row.Body(winrt::hstring{ body });
            }
            else
            {
                row.Kind(Control::HyperlinkPreviewTabKind::Comments);

                const auto value = reader.Json(tab.Path);
                if (!value || value.ValueType() != JsonValueType::Array)
                {
                    continue;
                }

                auto comments = winrt::single_threaded_vector<Control::HyperlinkPreviewComment>();
                const auto array = value.GetArray();
                // Bounded: a long-running issue can carry hundreds, and no
                // surface here wants to render them all.
                const auto count = std::min(array.Size(), 50u);
                for (uint32_t i = 0; i < count; ++i)
                {
                    const auto item = array.GetAt(i);

                    std::wstring body;
                    const auto rawBody = At(item, tab.ItemBodyPath);
                    if (flatten)
                    {
                        FlattenAdf(rawBody, body, 0);
                    }
                    else
                    {
                        body = ValueToString(rawBody);
                    }
                    body = TrimTrailingBlank(std::move(body));

                    const auto author = ValueToString(At(item, tab.ItemAuthorPath));
                    if (body.empty() && author.empty())
                    {
                        continue;
                    }

                    Control::HyperlinkPreviewComment comment{};
                    comment.Author(winrt::hstring{ author });
                    comment.AvatarUri(winrt::hstring{ ValueToString(At(item, tab.ItemAvatarPath)) });
                    comment.Body(winrt::hstring{ body });
                    // RelativeTime hands back whatever it was given when it
                    // cannot parse it, so an unexpected shape still shows.
                    if (const auto when = ValueToString(At(item, tab.ItemTimePath)); !when.empty())
                    {
                        comment.Time(winrt::hstring{ RelativeTime(when) });
                    }
                    comments.Append(comment);
                }

                if (comments.Size() == 0)
                {
                    continue;
                }
                row.Comments(comments);
            }

            tabs.Append(row);
        }

        preview.Tabs(tabs);
    }

    // ---- actions --------------------------------------------------------

    // Jira spells a transition's demands as an object keyed by field name:
    //   "fields": { "resolution": { "required": true, "name": "Resolution",
    //                               "allowedValues": [ { "name": "Done" } ] } }
    // Anything shaped like that maps; anything else contributes no fields,
    // which simply means the action is offered without a form.
    IVector<Control::HyperlinkPreviewActionField> BuildOptionFields(const IJsonValue& spec)
    {
        auto fields = winrt::single_threaded_vector<Control::HyperlinkPreviewActionField>();
        if (!spec || spec.ValueType() != JsonValueType::Object)
        {
            return fields;
        }

        for (const auto& pair : spec.GetObject())
        {
            const auto key = pair.Key();
            const auto value = pair.Value();
            if (key.empty() || !value || value.ValueType() != JsonValueType::Object)
            {
                continue;
            }
            const auto detail = value.GetObject();

            Control::HyperlinkPreviewActionField field{};
            field.Key(key);
            auto label = detail.GetNamedString(L"name", L"");
            field.Label(label.empty() ? key : label);
            field.Required(detail.GetNamedBoolean(L"required", false));

            auto choices = winrt::single_threaded_vector<winrt::hstring>();
            if (const auto allowed = detail.GetNamedArray(L"allowedValues", nullptr))
            {
                for (uint32_t i = 0; i < allowed.Size(); ++i)
                {
                    const auto entry = allowed.GetAt(i);
                    if (!entry || entry.ValueType() != JsonValueType::Object)
                    {
                        continue;
                    }
                    const auto option = entry.GetObject();
                    auto text = option.GetNamedString(L"name", L"");
                    if (text.empty())
                    {
                        text = option.GetNamedString(L"value", L"");
                    }
                    if (text.empty())
                    {
                        text = option.GetNamedString(L"id", L"");
                    }
                    if (!text.empty())
                    {
                        choices.Append(text);
                    }
                }
            }
            field.Options(choices);

            fields.Append(field);
        }
        return fields;
    }

    Control::HyperlinkPreviewActionOption BuildOption(const Snapshot::Action& action, const IJsonValue& element)
    {
        Control::HyperlinkPreviewActionOption option{};
        option.Id(winrt::hstring{ ValueToString(At(element, action.OptionIdPath)) });
        option.Label(winrt::hstring{ ValueToString(At(element, action.OptionLabelPath)) });
        option.Badge(winrt::hstring{ ValueToString(At(element, action.OptionBadgePath)) });
        option.Color(winrt::hstring{ ValueToString(At(element, action.OptionColorPath)) });
        option.Fields(BuildOptionFields(At(element, action.OptionFieldsPath)));
        return option;
    }

    void BuildActions(const Snapshot::Plugin& plugin, const ResultReader& reader, const Control::HyperlinkPreview& preview)
    {
        auto actions = winrt::single_threaded_vector<Control::HyperlinkPreviewAction>();

        for (const auto& action : plugin.Actions)
        {
            Control::HyperlinkPreviewAction row{};
            row.Key(winrt::hstring{ action.Key });
            row.Label(winrt::hstring{ action.Label });

            auto options = winrt::single_threaded_vector<Control::HyperlinkPreviewActionOption>();
            if (action.IsChoice)
            {
                const auto value = reader.Json(action.OptionsPath);
                if (!value || value.ValueType() != JsonValueType::Array)
                {
                    // Nothing to choose from -- most often because the step
                    // that would have listed the options never ran.
                    continue;
                }
                const auto array = value.GetArray();
                for (uint32_t i = 0; i < array.Size(); ++i)
                {
                    auto option = BuildOption(action, array.GetAt(i));
                    if (option.Id().empty())
                    {
                        continue;
                    }
                    if (option.Label().empty())
                    {
                        option.Label(option.Id());
                    }
                    options.Append(option);
                }
                if (options.Size() == 0)
                {
                    continue;
                }
            }
            row.Options(options);

            actions.Append(row);
        }

        preview.Actions(actions);
    }

    struct RepoOwnerResolution
    {
        std::wstring Owner;
        bool IsPull{ false };
    };
    static std::map<std::wstring, RepoOwnerResolution> s_resolvedRepoOwners;
    static std::mutex s_resolvedRepoOwnersMutex;

    // Runs the whole pipeline for one integration and renders the result.
    // Blocking; always called from a background thread.
    Control::HyperlinkPreview RunFetch(const Snapshot::Plugin& plugin, const std::wstring& text, const TemplateValueMap& groups)
    {
        Control::HyperlinkPreview preview{};
        preview.IntegrationId(winrt::hstring{ plugin.Id });
        preview.IntegrationName(winrt::hstring{ plugin.Name });
        preview.IntegrationIcon(winrt::hstring{ plugin.Icon });
        // Echoed back so a refresh or an action can be run against the same
        // thing without the caller having to remember what produced it.
        preview.SourceText(winrt::hstring{ text });

        auto fields = winrt::single_threaded_vector<Control::HyperlinkPreviewField>();
        preview.Fields(fields);

        TemplateValueMap effectiveGroups = groups;
        WWH::HttpClient plainClient{ nullptr };
        WWH::HttpClient lenientClient{ nullptr };

        if (plugin.Id == L"github" && effectiveGroups.find(L"owner") == effectiveGroups.end())
        {
            const auto repoIt = effectiveGroups.find(L"repo");
            const auto numberIt = effectiveGroups.find(L"number");
            if (repoIt != effectiveGroups.end() && numberIt != effectiveGroups.end())
            {
                const auto& repo = repoIt->second;
                const auto& number = numberIt->second;

                RepoOwnerResolution resolution;
                bool resolved = false;
                {
                    std::lock_guard lock{ s_resolvedRepoOwnersMutex };
                    const auto cached = s_resolvedRepoOwners.find(repo);
                    if (cached != s_resolvedRepoOwners.end())
                    {
                        resolution = cached->second;
                        resolved = true;
                    }
                }

                if (!resolved)
                {
                    std::wstring token;
                    const auto credIt = plugin.Credentials.find(L"token");
                    if (credIt != plugin.Credentials.end() && !credIt->second.empty())
                    {
                        token = credIt->second;
                    }
                    else
                    {
                        const auto ghOutput = ::TerminalApp::RunProcessCapture(L"pwsh -NoProfile -NonInteractive -Command \"(gh auth token 2>$null | Out-String).Trim()\"", {}, 4000);
                        token = til::u8u16(ghOutput);
                        while (!token.empty() && (token.back() == L'\r' || token.back() == L'\n' || token.back() == L' '))
                        {
                            token.pop_back();
                        }
                    }

                    std::wstring candidates;
                    const auto settingIt = plugin.Settings.find(L"candidateOwners");
                    if (settingIt != plugin.Settings.end())
                    {
                        candidates = settingIt->second;
                    }

                    std::vector<std::wstring> candidateList;
                    std::wstring current;
                    for (wchar_t ch : candidates)
                    {
                        if (ch == L',')
                        {
                            auto trimmed = std::wstring{ Trim(current) };
                            if (!trimmed.empty())
                            {
                                candidateList.push_back(std::move(trimmed));
                            }
                            current.clear();
                        }
                        else
                        {
                            current.push_back(ch);
                        }
                    }
                    auto lastTrimmed = std::wstring{ Trim(current) };
                    if (!lastTrimmed.empty())
                    {
                        candidateList.push_back(std::move(lastTrimmed));
                    }

                    ExpandContext probeContext;
                    for (const auto& candidate : candidateList)
                    {
                        HttpCall probeCall;
                        probeCall.Url = fmt::format(L"https://api.github.com/repos/{}/{}/issues/{}", candidate, repo, number);
                        probeCall.Method = L"GET";
                        std::vector<std::pair<std::wstring, std::wstring>> headers = {
                            { L"Accept", L"application/vnd.github+json" },
                            { L"X-GitHub-Api-Version", L"2022-11-28" },
                            { L"User-Agent", L"WindowsTerminal" }
                        };
                        probeCall.Headers = &headers;
                        if (!token.empty())
                        {
                            probeCall.AuthType = L"bearer";
                            probeCall.AuthPassword = token;
                        }
                        probeCall.TimeoutMs = 4000;

                        const auto outcome = RunHttpCall(probeCall, probeContext, plugin.Name, plainClient, lenientClient);
                        if (outcome.Error.empty() && !outcome.Body.empty())
                        {
                            JsonObject parsedObj{ nullptr };
                            if (JsonObject::TryParse(winrt::hstring{ outcome.Body }, parsedObj))
                            {
                                resolution.Owner = candidate;
                                resolution.IsPull = parsedObj.HasKey(L"pull_request");
                                resolved = true;
                                {
                                    std::lock_guard lock{ s_resolvedRepoOwnersMutex };
                                    s_resolvedRepoOwners[repo] = resolution;
                                }
                                break;
                            }
                        }
                    }
                }

                if (resolved)
                {
                    effectiveGroups[L"owner"] = resolution.Owner;
                    if (resolution.IsPull)
                    {
                        effectiveGroups[L"ispull"] = L"pull";
                        preview.ResolvedUri(winrt::hstring{ fmt::format(L"https://github.com/{}/{}/pull/{}", resolution.Owner, repo, number) });
                    }
                    else
                    {
                        effectiveGroups[L"isissue"] = L"issues";
                        preview.ResolvedUri(winrt::hstring{ fmt::format(L"https://github.com/{}/{}/issues/{}", resolution.Owner, repo, number) });
                    }
                }
            }
        }

        PipelineOutcome pipeline;

        ExpandContext context;
        context.Groups = &effectiveGroups;
        context.Settings = &plugin.Settings;
        context.Credentials = &plugin.Credentials;
        context.Results = &pipeline.Results;

        RunPipeline(plugin, context, pipeline, plainClient, lenientClient);

        const ResultReader reader{ &pipeline };

        // Which group each field belongs to. A manifest declares the
        // relationship the other way round -- a group lists the field keys it
        // owns -- so it is inverted once here rather than searched per field.
        // A key no group claims simply never hits, which is what leaves both
        // Group and GroupLabel empty on an ungrouped field.
        std::map<std::wstring, std::pair<std::wstring, std::wstring>> groupOfField;
        for (const auto& group : plugin.FieldGroups)
        {
            for (const auto& key : group.Fields)
            {
                groupOfField.try_emplace(key, group.Key, group.Label);
            }
        }

        for (const auto& field : plugin.Fields)
        {
            auto value = reader.Text(field.Path);
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
            // Model::IntegrationFieldKind and Control::HyperlinkPreviewFieldKind
            // declare the same members in the same order on purpose, so the two
            // sides can be cast rather than switched over.
            row.Kind(static_cast<Control::HyperlinkPreviewFieldKind>(field.Kind));
            if (!field.IconPath.empty())
            {
                row.IconUri(winrt::hstring{ reader.Text(field.IconPath) });
            }
            auto color = field.ColorPath.empty() ? std::wstring{} : reader.Text(field.ColorPath);
            if (color.empty())
            {
                color = field.Color;
            }
            row.Color(winrt::hstring{ color });
            // A field no group claimed keeps both empty, which is what a
            // surface with room reads as "no heading".
            if (const auto group = groupOfField.find(field.Key); group != groupOfField.end())
            {
                row.Group(winrt::hstring{ group->second.first });
                row.GroupLabel(winrt::hstring{ group->second.second });
            }
            fields.Append(row);
        }

        BuildTabs(plugin, reader, preview);
        BuildActions(plugin, reader, preview);

        if (!plugin.Html.empty())
        {
            // Handed over untouched: what to do with it is the control's call.
            preview.Html(winrt::hstring{ plugin.Html });

            // Only the HTML representation ever needs the raw step results --
            // the fields path has already pulled out everything it wanted
            // through JSON pointers -- so this is built only when there is an
            // html template to hand it to. One object keyed by step id, exactly
            // as a "stepId:/pointer" path spells it, even when a single step
            // ran; a step that declared no id contributes nothing, because
            // nothing could address it.
            if (!pipeline.Results.empty())
            {
                JsonObject data;
                for (const auto& [stepId, value] : pipeline.Results)
                {
                    data.SetNamedValue(winrt::hstring{ stepId }, value);
                }
                preview.DataJson(data.Stringify());
            }
        }

        auto error = pipeline.Error;
        if (error.empty() &&
            fields.Size() == 0 &&
            preview.Tabs().Size() == 0 &&
            preview.Actions().Size() == 0 &&
            !pipeline.OptionalErrors.empty())
        {
            // Everything that ran was allowed to fail and did, and there is
            // nothing to show -- so the reason one of them failed is the most
            // useful thing left to say.
            error = pipeline.OptionalErrors.front();
        }
        if (!error.empty())
        {
            preview.Error(winrt::hstring{ error });
        }
        return preview;
    }

    // ---- actions, run ---------------------------------------------------

    // The shape the far end wants one of an action's extra values in.
    //
    // A value has to be spelled the way its own field spelled it: Jira takes a
    // resolution as {"name":"Done"}, a select custom field as {"value":"..."},
    // and free text as a bare string. The submitted text came out of that
    // field's allowedValues in the first place (BuildOptionFields picks
    // name, then value, then id), so it is matched back to the entry it was
    // taken from and re-wrapped under whichever key matched.
    IJsonValue ActionFieldValue(const IJsonValue& fieldSpec, const std::wstring& text)
    {
        if (fieldSpec && fieldSpec.ValueType() == JsonValueType::Object)
        {
            const auto detail = fieldSpec.GetObject();
            if (const auto allowed = detail.GetNamedArray(L"allowedValues", nullptr))
            {
                for (uint32_t i = 0; i < allowed.Size(); ++i)
                {
                    const auto entry = allowed.GetAt(i);
                    if (!entry || entry.ValueType() != JsonValueType::Object)
                    {
                        continue;
                    }
                    const auto option = entry.GetObject();
                    for (const auto key : { L"name", L"value", L"id" })
                    {
                        const auto candidate = option.GetNamedString(key, L"");
                        if (candidate.empty() || std::wstring{ candidate } != text)
                        {
                            continue;
                        }
                        JsonObject wrapped;
                        wrapped.SetNamedValue(key, JsonValue::CreateStringValue(candidate));
                        return wrapped;
                    }
                }

                // A choice whose text matched nothing that was offered. It is
                // still a choice field, so it is sent the way the great
                // majority of them are spelled rather than as a bare string.
                JsonObject wrapped;
                wrapped.SetNamedValue(L"name", JsonValue::CreateStringValue(winrt::hstring{ text }));
                return wrapped;
            }
        }
        return JsonValue::CreateStringValue(winrt::hstring{ text });
    }

    // What the user filled in, as the object the far end expects. `spec` is the
    // chosen option's own fields declaration, or null when it declared none.
    JsonObject BuildActionFields(const IJsonValue& spec, const TemplateValueMap& values)
    {
        JsonObject fields;
        for (const auto& [key, text] : values)
        {
            if (key.empty() || text.empty())
            {
                continue;
            }

            IJsonValue fieldSpec{ nullptr };
            if (spec && spec.ValueType() == JsonValueType::Object)
            {
                const auto object = spec.GetObject();
                const winrt::hstring name{ key };
                if (object.HasKey(name))
                {
                    fieldSpec = object.Lookup(name);
                }
            }
            fields.SetNamedValue(winrt::hstring{ key }, ActionFieldValue(fieldSpec, text));
        }
        return fields;
    }

    // Puts those values into the request as a top-level "fields" object, beside
    // whatever the manifest's template produced. Done by parsing and
    // re-serialising rather than by splicing strings, so the quoting is the
    // JSON writer's problem and not a regex's.
    //
    // A body that is not a JSON object is left exactly as written: there is
    // nothing safe to merge into, and the far end gets to say what it makes of
    // the request rather than this silently rewriting it.
    std::wstring MergeActionFields(std::wstring body, const JsonObject& extra)
    {
        if (!extra || extra.Size() == 0)
        {
            return body;
        }

        JsonObject parsed{ nullptr };
        if (!body.empty() && !JsonObject::TryParse(winrt::hstring{ body }, parsed))
        {
            return body;
        }
        if (!parsed)
        {
            parsed = JsonObject{};
        }

        // A template that already wrote "fields" keeps what it put there; the
        // user's answers are added beside it.
        JsonObject target{ nullptr };
        if (parsed.HasKey(L"fields"))
        {
            if (const auto existing = parsed.Lookup(L"fields"); existing && existing.ValueType() == JsonValueType::Object)
            {
                target = existing.GetObject();
            }
        }
        if (!target)
        {
            target = JsonObject{};
        }

        for (const auto& pair : extra)
        {
            target.SetNamedValue(pair.Key(), pair.Value());
        }
        parsed.SetNamedValue(L"fields", target);
        return std::wstring{ parsed.Stringify() };
    }

    // Runs one action end to end: re-runs the fetch pipeline so the action's
    // templates have the same results the card was built from, fires the
    // action's own request, and then looks for a way back.
    //
    // Blocking; always called from a background thread. The request itself is
    // guarded here, so a normal failure comes back as an error line; anything
    // else is caught by InvokeActionAsync, which is what keeps an unexpected
    // WinRT failure from escaping into a coroutine.
    Control::HyperlinkActionResult RunAction(const Snapshot::Plugin& plugin,
                                             const Snapshot::Action& action,
                                             const TemplateValueMap& groups,
                                             const std::wstring& choiceId,
                                             const TemplateValueMap& fieldValues)
    {
        Control::HyperlinkActionResult result{};

        // {{choice}} is the picked option's id. It joins the matcher's own
        // capture groups rather than getting a namespace of its own, so a
        // manifest spells it the same way it spells {{issue}}.
        auto templateGroups = groups;
        templateGroups[L"choice"] = choiceId;

        PipelineOutcome pipeline;

        ExpandContext context;
        context.Groups = &templateGroups;
        context.Settings = &plugin.Settings;
        context.Credentials = &plugin.Credentials;
        context.Results = &pipeline.Results;
        context.ActionFields = &fieldValues;

        WWH::HttpClient plainClient{ nullptr };
        WWH::HttpClient lenientClient{ nullptr };
        RunPipeline(plugin, context, pipeline, plainClient, lenientClient);

        if (!pipeline.Error.empty())
        {
            result.Error(winrt::hstring{ pipeline.Error });
            return result;
        }

        const ResultReader reader{ &pipeline };

        // Read before the change, so the undo below has something to aim at.
        const auto stateBefore = action.CurrentStatePath.empty() ? std::wstring{} : reader.Text(action.CurrentStatePath);

        // What the chosen option said it needed, so each answer can be shaped
        // the way its own field wants it.
        IJsonValue chosenFields{ nullptr };
        if (!fieldValues.empty() && action.IsChoice && !action.OptionsPath.empty() && !action.OptionFieldsPath.empty())
        {
            if (const auto options = reader.Json(action.OptionsPath); options && options.ValueType() == JsonValueType::Array)
            {
                const auto array = options.GetArray();
                for (uint32_t i = 0; i < array.Size(); ++i)
                {
                    const auto element = array.GetAt(i);
                    if (ValueToString(At(element, action.OptionIdPath)) == choiceId)
                    {
                        chosenFields = At(element, action.OptionFieldsPath);
                        break;
                    }
                }
            }
        }

        // Expanded here rather than inside RunHttpCall, because the user's
        // answers cannot be baked into a static template: which fields a Jira
        // transition demands differs per transition, so the body carries only
        // {"transition":{"id":"{{choice}}"}} and the answers are merged into
        // the expanded JSON afterwards.
        //
        // Escape::Json for the expansion, since {{choice}} and any
        // {{field.<key>}} the manifest DID spell out land inside string
        // literals; the merge below quotes its own values.
        auto body = Expand(action.Body, context, Escape::Json);
        if (!fieldValues.empty())
        {
            body = MergeActionFields(std::move(body), BuildActionFields(chosenFields, fieldValues));
        }

        HttpCall call;
        call.Url = action.Url;
        call.Method = action.Method.empty() ? std::wstring_view{ L"POST" } : std::wstring_view{ action.Method };
        call.Headers = &action.Headers;
        call.AuthType = action.AuthType;
        call.AuthUser = action.AuthUser;
        call.AuthPassword = action.AuthPassword;
        call.AllowUntrusted = action.AllowUntrusted;
        call.TimeoutMs = action.TimeoutMs;
        call.BodyLiteral = &body;

        StepOutcome outcome;
        try
        {
            outcome = RunHttpCall(call, context, plugin.Name, plainClient, lenientClient);
        }
        catch (const winrt::hresult_error& e)
        {
            outcome.Error = fmt::format(L"{}: {}", plugin.Name, std::wstring{ e.message() });
        }
        catch (...)
        {
            outcome.Error = fmt::format(L"{}: the request failed", plugin.Name);
        }

        if (!outcome.Error.empty())
        {
            result.Error(winrt::hstring{ outcome.Error });
            return result;
        }
        result.Ok(true);

        // The way back, when the far end offers one: an option that moves the
        // thing to the state it was in before the change.
        //
        // Looked up AFTER the change, not before, and that is the whole trick:
        // Jira lists the transitions available FROM the current status, so the
        // one that returns to the previous status can only appear once the
        // change has happened. Searching the pre-action list would only ever
        // find a self-loop. It costs one more pipeline run, which the card is
        // about to pay for anyway when it refreshes.
        //
        // Often there is no way back at all -- a workflow that only moves
        // forward -- and then both fields stay empty, which the card is
        // expected to report honestly rather than paper over.
        if (action.IsChoice && !stateBefore.empty() && !action.OptionTargetIdPath.empty() && !action.OptionsPath.empty())
        {
            PipelineOutcome after;
            auto afterContext = context;
            afterContext.Results = &after.Results;
            RunPipeline(plugin, afterContext, after, plainClient, lenientClient);

            const ResultReader afterReader{ &after };
            const auto value = afterReader.Json(action.OptionsPath);
            if (value && value.ValueType() == JsonValueType::Array)
            {
                const auto array = value.GetArray();
                for (uint32_t i = 0; i < array.Size(); ++i)
                {
                    const auto element = array.GetAt(i);
                    if (ValueToString(At(element, action.OptionTargetIdPath)) != stateBefore)
                    {
                        continue;
                    }
                    const auto id = ValueToString(At(element, action.OptionIdPath));
                    if (id.empty() || id == choiceId)
                    {
                        continue;
                    }
                    auto label = ValueToString(At(element, action.OptionLabelPath));
                    if (label.empty())
                    {
                        label = id;
                    }
                    result.UndoChoiceId(winrt::hstring{ id });
                    result.UndoLabel(winrt::hstring{ label });
                    break;
                }
            }
        }

        return result;
    }
}

namespace winrt::TerminalApp::implementation
{
    void HyperlinkPreviewService::Rebuild(const Model::CascadiaSettings& settings, const Model::WindowSettings& windowSettings)
    try
    {
        auto snapshot = std::make_shared<HyperlinkPreviewSnapshot>();

        // GlobalAppSettings::Integrations() never hands back null: it is an
        // inheritable setting whose default is MakeIntegrationSettingsMap(), so
        // an unset "integrations" materialises a fresh empty map on every read.
        // There is therefore nothing to null-check -- the case worth testing is
        // an EMPTY map, which is what "no integrations configured" looks like,
        // and which means no previews and no reason to walk the registry.
        IMap<hstring, Model::IntegrationSettings> configured{ nullptr };
        IVector<Model::IntegrationManifest> manifests{ nullptr };
        if (settings)
        {
            if (const auto globals = settings.GlobalSettings())
            {
                configured = globals.Integrations();
                if (configured.Size() > 0)
                {
                    manifests = Model::IntegrationRegistry::All();
                }
            }
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

                if (const auto settingFields = manifest.Settings())
                {
                    for (const auto& field : settingFields)
                    {
                        if (!field || field.Key().empty())
                        {
                            continue;
                        }
                        if (!field.DefaultValue().empty())
                        {
                            plugin->Settings[std::wstring{ field.Key() }] = std::wstring{ field.DefaultValue() };
                        }
                        else if (!field.Placeholder().empty() &&
                                 (field.Placeholder().starts_with(L"http://") || field.Placeholder().starts_with(L"https://")))
                        {
                            plugin->Settings[std::wstring{ field.Key() }] = std::wstring{ field.Placeholder() };
                        }
                    }
                }

                if (const auto values = entry.Values())
                {
                    for (const auto& pair : values)
                    {
                        if (!pair.Value().empty())
                        {
                            plugin->Settings[std::wstring{ pair.Key() }] = std::wstring{ pair.Value() };
                        }
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
                            // Nothing stored, or the vault refused.
                        }
                        // Only a REQUIRED credential decides whether the
                        // integration is configured, exactly as the settings
                        // loop above treats a required setting. GitHub declares
                        // its token optional because `gh auth token` is tried
                        // first; without this test an integration that works
                        // perfectly through the CLI would be permanently "not
                        // configured" and never fetch anything.
                        if (value.empty() && field.Required())
                        {
                            configuredFully = false;
                        }
                        // Stored either way, empty included: {{credentials.x}}
                        // expanding to nothing is what makes a step's `when` /
                        // `unless` work as a presence test.
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
                        entryStep.Optional = step.Optional();
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

                if (const auto groups = manifest.FieldGroups())
                {
                    for (const auto& group : groups)
                    {
                        if (!group)
                        {
                            continue;
                        }
                        HyperlinkPreviewSnapshot::FieldGroup entryGroup;
                        entryGroup.Key = std::wstring{ group.Key() };
                        entryGroup.Label = std::wstring{ group.Label() };
                        if (const auto members = group.Fields())
                        {
                            for (const auto& key : members)
                            {
                                entryGroup.Fields.push_back(std::wstring{ key });
                            }
                        }
                        plugin->FieldGroups.push_back(std::move(entryGroup));
                    }
                }

                // Same contract as Fields: a null Tabs collection means "the
                // manifest's own defaults", an empty one means the user turned
                // every tab off.
                const auto chosenTabs = entry.Tabs();
                std::set<std::wstring> selectedTabs;
                if (chosenTabs)
                {
                    for (const auto& key : chosenTabs)
                    {
                        selectedTabs.insert(std::wstring{ key });
                    }
                }

                if (const auto tabs = manifest.Tabs())
                {
                    for (const auto& tab : tabs)
                    {
                        if (!tab)
                        {
                            continue;
                        }
                        const std::wstring key{ tab.Key() };
                        const auto visible = chosenTabs ? selectedTabs.count(key) != 0 : tab.DefaultVisible();
                        if (!visible)
                        {
                            continue;
                        }

                        HyperlinkPreviewSnapshot::Tab entryTab;
                        entryTab.Key = key;
                        entryTab.Label = std::wstring{ tab.Label() };
                        // NOT a static_cast, unlike the field kinds.
                        // Model::IntegrationTabKind is { Body, List } and
                        // Control::HyperlinkPreviewTabKind is { Fields, Body,
                        // Comments } -- the control has a member for the
                        // built-in field list that no manifest can ask for, so
                        // the two enums do not line up and the mapping is
                        // written out (see BuildTabs).
                        entryTab.IsList = tab.Kind() == Model::IntegrationTabKind::List;
                        entryTab.Path = std::wstring{ tab.Path() };
                        entryTab.Format = std::wstring{ tab.Format() };
                        entryTab.ItemAuthorPath = std::wstring{ tab.ItemAuthorPath() };
                        entryTab.ItemAvatarPath = std::wstring{ tab.ItemAvatarPath() };
                        entryTab.ItemBodyPath = std::wstring{ tab.ItemBodyPath() };
                        entryTab.ItemTimePath = std::wstring{ tab.ItemTimePath() };
                        plugin->Tabs.push_back(std::move(entryTab));
                    }
                }

                if (const auto actions = manifest.Actions())
                {
                    for (const auto& action : actions)
                    {
                        if (!action || action.Key().empty())
                        {
                            continue;
                        }
                        HyperlinkPreviewSnapshot::Action entryAction;
                        entryAction.Key = std::wstring{ action.Key() };
                        entryAction.Label = std::wstring{ action.Label() };
                        entryAction.IsChoice = action.Kind() == Model::IntegrationActionKind::Choice;
                        entryAction.OptionsPath = std::wstring{ action.OptionsPath() };
                        entryAction.OptionIdPath = std::wstring{ action.OptionIdPath() };
                        entryAction.OptionLabelPath = std::wstring{ action.OptionLabelPath() };
                        entryAction.OptionBadgePath = std::wstring{ action.OptionBadgePath() };
                        entryAction.OptionColorPath = std::wstring{ action.OptionColorPath() };
                        entryAction.OptionTargetIdPath = std::wstring{ action.OptionTargetIdPath() };
                        entryAction.CurrentStatePath = std::wstring{ action.CurrentStatePath() };
                        entryAction.OptionFieldsPath = std::wstring{ action.OptionFieldsPath() };
                        entryAction.Method = std::wstring{ action.Method() };
                        entryAction.Url = std::wstring{ action.Url() };
                        entryAction.Body = std::wstring{ action.Body() };
                        entryAction.AuthType = std::wstring{ action.AuthType() };
                        entryAction.AuthUser = std::wstring{ action.AuthUser() };
                        entryAction.AuthPassword = std::wstring{ action.AuthPassword() };
                        entryAction.AllowUntrusted = action.AllowUntrustedCertificate();
                        entryAction.TimeoutMs = action.TimeoutMs() > 0 ? static_cast<unsigned long>(action.TimeoutMs()) : 8000ul;

                        if (const auto headers = action.Headers())
                        {
                            for (const auto& pair : headers)
                            {
                                entryAction.Headers.emplace_back(std::wstring{ pair.Key() }, std::wstring{ pair.Value() });
                            }
                        }
                        plugin->Actions.push_back(std::move(entryAction));
                    }
                }

                if (const auto patterns = manifest.DetectPatterns())
                {
                    for (const auto& pattern : patterns)
                    {
                        if (!pattern.empty())
                        {
                            plugin->DetectPatterns.push_back(std::wstring{ pattern });
                        }
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

    void HyperlinkPreviewService::_cacheErase(const std::wstring& key)
    {
        std::scoped_lock lock{ _mutex };
        _cache.erase(key);
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
            if (found && found->Owner && found->Owner->Id == L"github")
            {
                const auto repoIt = found->Groups.find(L"repo");
                const auto numberIt = found->Groups.find(L"number");
                if (repoIt != found->Groups.end() && numberIt != found->Groups.end())
                {
                    std::lock_guard lock{ s_resolvedRepoOwnersMutex };
                    const auto it = s_resolvedRepoOwners.find(repoIt->second);
                    if (it != s_resolvedRepoOwners.end())
                    {
                        const auto& res = it->second;
                        return winrt::hstring{ fmt::format(L"https://github.com/{}/{}/{}/{}",
                            res.Owner, repoIt->second, res.IsPull ? L"pull" : L"issues", numberIt->second) };
                    }
                }
            }
            return {};
        }

        ExpandContext context;
        context.Groups = &found->Groups;
        context.Settings = &found->Owner->Settings;
        context.Credentials = &found->Owner->Credentials;
        return hstring{ Expand(found->Matcher->LinkTemplate, context, Escape::None) };
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
        return _previewAsync(std::move(text), std::move(integrationHint), false);
    }

    // The same fetch, with whatever is cached thrown away first -- what to call
    // once an action has changed the thing the card is describing.
    IAsyncOperation<Control::HyperlinkPreview> HyperlinkPreviewService::RefreshAsync(hstring text, hstring integrationHint)
    {
        return _previewAsync(std::move(text), std::move(integrationHint), true);
    }

    IAsyncOperation<Control::HyperlinkPreview> HyperlinkPreviewService::_previewAsync(hstring text, hstring integrationHint, bool bypassCache)
    {
        auto strongThis = get_strong();

        // Resolved here, on the caller's thread, and then copied out: the
        // snapshot may be replaced by a settings reload while this request is
        // in flight, and the plugin this fetch belongs to must not change
        // underneath it.
        std::shared_ptr<HyperlinkPreviewSnapshot::Plugin> plugin;
        TemplateValueMap groups;
        std::wstring source;
        std::wstring cacheKey;
        Control::HyperlinkPreview cached{ nullptr };

        try
        {
            source = std::wstring{ text };
            if (auto found = FindMatch(strongThis->_currentSnapshot(), source, std::wstring{ integrationHint }, false);
                found && found->Owner->Configured && !found->Owner->Steps.empty())
            {
                plugin = found->Owner;
                groups = std::move(found->Groups);
                cacheKey = plugin->Id + L"|" + source;
                if (bypassCache)
                {
                    strongThis->_cacheErase(cacheKey);
                }
                else
                {
                    cached = strongThis->_cacheLookup(cacheKey);
                }
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
            broken.SourceText(hstring{ source });
            broken.Fields(winrt::single_threaded_vector<Control::HyperlinkPreviewField>());
            broken.Tabs(winrt::single_threaded_vector<Control::HyperlinkPreviewTab>());
            broken.Actions(winrt::single_threaded_vector<Control::HyperlinkPreviewAction>());
            broken.Error(hstring{ message });
            return broken;
        };

        Control::HyperlinkPreview preview{ nullptr };
        try
        {
            preview = RunFetch(*plugin, source, groups);
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

    IAsyncOperation<Control::HyperlinkActionResult> HyperlinkPreviewService::InvokeActionAsync(hstring text,
                                                                                              hstring integrationHint,
                                                                                              hstring actionKey,
                                                                                              hstring choiceId,
                                                                                              IMap<hstring, hstring> fieldValues)
    {
        auto strongThis = get_strong();

        // Same discipline as a preview: everything is resolved and copied on
        // the caller's thread, so a settings reload mid-flight cannot pull the
        // plugin (or the action) out from under the request.
        std::shared_ptr<HyperlinkPreviewSnapshot::Plugin> plugin;
        const HyperlinkPreviewSnapshot::Action* action{ nullptr };
        TemplateValueMap groups;
        TemplateValueMap values;
        std::wstring cacheKey;
        std::wstring failure;

        try
        {
            const std::wstring source{ text };
            const std::wstring key{ actionKey };
            if (auto found = FindMatch(strongThis->_currentSnapshot(), source, std::wstring{ integrationHint }, false);
                found && found->Owner->Configured)
            {
                for (const auto& candidate : found->Owner->Actions)
                {
                    if (candidate.Key == key)
                    {
                        action = &candidate;
                        break;
                    }
                }
                if (action)
                {
                    plugin = found->Owner;
                    groups = std::move(found->Groups);
                    cacheKey = plugin->Id + L"|" + source;
                }
            }

            if (!action)
            {
                failure = L"That action is no longer available.";
            }
            else if (fieldValues)
            {
                for (const auto& pair : fieldValues)
                {
                    values[std::wstring{ pair.Key() }] = std::wstring{ pair.Value() };
                }
            }
        }
        catch (...)
        {
            LOG_CAUGHT_EXCEPTION();
            failure = L"That action could not be started.";
        }

        if (!plugin || !action)
        {
            Control::HyperlinkActionResult refused{};
            refused.Error(hstring{ failure.empty() ? std::wstring{ L"That action is no longer available." } : failure });
            co_return refused;
        }

        // `plugin` and `action` both live in the coroutine frame across the
        // suspension below, and a published snapshot is never mutated, so the
        // pointer into the plugin's Actions vector stays good even if a
        // settings reload replaces the snapshot while the request is in flight.
        const std::wstring choice{ choiceId };

        co_await winrt::resume_background();

        Control::HyperlinkActionResult result{ nullptr };
        try
        {
            result = RunAction(*plugin, *action, groups, choice, values);
        }
        catch (const winrt::hresult_error& e)
        {
            result = Control::HyperlinkActionResult{};
            result.Error(hstring{ fmt::format(L"{}: {}", plugin->Name, std::wstring{ e.message() }) });
        }
        catch (...)
        {
            result = Control::HyperlinkActionResult{};
            result.Error(hstring{ fmt::format(L"{}: the action could not be run", plugin->Name) });
        }

        // Whatever the card is showing described the state before the action.
        // Even a failed one may have got far enough to change something, so the
        // entry goes either way and the next read repopulates it.
        strongThis->_cacheErase(cacheKey);

        co_return result;
    }
}
