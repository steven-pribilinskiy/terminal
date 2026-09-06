// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <string_view>
#include <vector>
#include <span>

#include <winrt/Microsoft.Terminal.Settings.Model.h>

namespace winrt::Microsoft::Terminal::Settings::Editor
{
    struct LinkTooltipPreset
    {
        std::wstring_view id;
        std::wstring_view name;
        std::wstring_view description;
        Model::HyperlinkMatchKind kind{ Model::HyperlinkMatchKind::Link };
        std::vector<std::wstring_view> schemes{};
        std::wstring_view pattern{};
        Model::HyperlinkFileTypeGroup fileTypeGroup{ Model::HyperlinkFileTypeGroup::None };
        std::vector<std::wstring_view> customExtensions{};
        std::wstring_view integration{};
        bool showPreview{ true };
    };

    inline std::span<const LinkTooltipPreset> GetLinkTooltipPresets() noexcept
    {
        static const LinkTooltipPreset presets[] = {
            {
                L"jira-issue-keys",
                L"Jira: Issue keys in output (Text)",
                L"Matches plain-text Jira issue keys like PROJ-123 in terminal output",
                Model::HyperlinkMatchKind::Text,
                {},
                LR"(\b(?<key>[A-Z][A-Z0-9]{1,9}-\d{1,7})\b)",
                Model::HyperlinkFileTypeGroup::None,
                {},
                L"jira",
                true,
            },
            {
                L"jira-links",
                L"Jira: Issue links (URL)",
                L"Matches Jira issue URLs (/browse/...)",
                Model::HyperlinkMatchKind::Link,
                { L"http", L"https" },
                LR"(^https?://[^/]+/browse/(?<key>[A-Z][A-Z0-9]+-\d+))",
                Model::HyperlinkFileTypeGroup::None,
                {},
                L"jira",
                true,
            },
            {
                L"github-prs-issues",
                L"GitHub: Pull requests & issues (URL)",
                L"Matches GitHub pull request and issue URLs",
                Model::HyperlinkMatchKind::Link,
                { L"https" },
                LR"(^https://github\.com/(?<owner>[^/]+)/(?<repo>[^/]+)/(?<ispull>pull|issues)/(?<number>\d+))",
                Model::HyperlinkFileTypeGroup::None,
                {},
                L"github",
                true,
            },
            {
                L"github-commits",
                L"GitHub: Commits (URL)",
                L"Matches GitHub commit URLs",
                Model::HyperlinkMatchKind::Link,
                { L"https" },
                LR"(^https://github\.com/(?<owner>[^/]+)/(?<repo>[^/]+)/commit/(?<sha>[0-9a-f]{7,40}))",
                Model::HyperlinkFileTypeGroup::None,
                {},
                L"github",
                true,
            },
            {
                L"github-repo-number",
                L"GitHub: Pull requests & issues (repo#number)",
                L"Matches repo#number text in terminal output and resolves against candidate owners",
                Model::HyperlinkMatchKind::Text,
                {},
                LR"(\b(?<repo>[A-Za-z0-9_.-]+)#(?<number>\d+)\b)",
                Model::HyperlinkFileTypeGroup::None,
                {},
                L"github",
                true,
            },
            {
                L"git-commit-hashes",
                L"Git: Commit hashes in output (Text)",
                L"Matches 7-40 character hexadecimal commit hashes in terminal text",
                Model::HyperlinkMatchKind::Text,
                {},
                LR"(\b[0-9a-f]{7,40}\b)",
                Model::HyperlinkFileTypeGroup::None,
                {},
                L"",
                false,
            },
            {
                L"slack-messages",
                L"Slack: Message links (URL)",
                L"Matches Slack message archive URLs",
                Model::HyperlinkMatchKind::Link,
                { L"https" },
                LR"(^https://(?<workspace>[a-zA-Z0-9_.-]+)\.slack\.com/archives/(?<channel>[A-Z0-9]+)/p(?<tsSeconds>\d{10})(?<tsMicros>\d{6}))",
                Model::HyperlinkFileTypeGroup::None,
                {},
                L"slack",
                true,
            },
            {
                L"media-preview",
                L"Media: Images, audio & video",
                L"Previews image, video, and audio links or files",
                Model::HyperlinkMatchKind::Link,
                { L"file", L"http", L"https" },
                L"",
                Model::HyperlinkFileTypeGroup::Media,
                {},
                L"",
                true,
            },
            {
                L"source-code-files",
                L"Source code files",
                L"Matches local source code files",
                Model::HyperlinkMatchKind::Link,
                { L"file" },
                L"",
                Model::HyperlinkFileTypeGroup::SourceCode,
                {},
                L"",
                true,
            },
            {
                L"stith-sessions",
                L"Stith: Agent session links",
                L"Matches Stith session and agent links",
                Model::HyperlinkMatchKind::Link,
                { L"stith", L"http", L"https" },
                LR"((?:^stith://(?:session|focus)/|^https?://[^/]+/(?:s|agent|sessions|embed/s)/)(?<id>[A-Za-z0-9-]{4,64}))",
                Model::HyperlinkFileTypeGroup::None,
                {},
                L"stith",
                true,
            },
        };
        return presets;
    }

    inline const LinkTooltipPreset* FindLinkTooltipPreset(std::wstring_view id) noexcept
    {
        for (const auto& preset : GetLinkTooltipPresets())
        {
            if (preset.id == id)
            {
                return &preset;
            }
        }
        return nullptr;
    }

    // A line each preset is meant to match, used to seed the rule editor's preview
    // so the card can show the rule picking something out of real text instead of
    // restating the regex the Pattern field already shows.
    //
    // Kept beside the presets rather than inside LinkTooltipPreset because it is
    // editor chrome: CreateRuleFromPreset writes none of it, and nothing about a
    // rule as saved depends on it. The two file-type presets have no sample
    // because they carry no pattern -- what they match is decided by the scheme
    // and file-type criteria, not by text.
    inline std::wstring_view GetLinkTooltipPresetSample(const std::wstring_view id) noexcept
    {
        struct Sample
        {
            std::wstring_view id;
            std::wstring_view text;
        };

        static constexpr Sample samples[] = {
            { L"jira-issue-keys", L"Deployed PROJ-1234 to staging" },
            { L"jira-links", L"https://acme.atlassian.net/browse/PROJ-1234" },
            { L"github-prs-issues", L"https://github.com/microsoft/terminal/pull/18920" },
            { L"github-commits", L"https://github.com/microsoft/terminal/commit/46100068f2a1" },
            { L"github-repo-number", L"See terminal#18920 for the details" },
            { L"git-commit-hashes", L"46100068 Record what the Settings crash actually was" },
            { L"slack-messages", L"https://acme.slack.com/archives/C01ABCD2EFG/p1717171717123456" },
            { L"stith-sessions", L"stith://session/9f3c1b2a-4d5e" },
        };

        for (const auto& sample : samples)
        {
            if (sample.id == id)
            {
                return sample.text;
            }
        }
        return {};
    }

    inline Model::HyperlinkTooltipRule CreateRuleFromPreset(const LinkTooltipPreset& preset)
    {
        Model::HyperlinkTooltipRule rule{};
        rule.Name(winrt::hstring{ preset.name });
        rule.Enabled(true);
        rule.Kind(preset.kind);
        rule.Pattern(winrt::hstring{ preset.pattern });
        if (!preset.schemes.empty())
        {
            std::vector<winrt::hstring> schemes;
            for (const auto& s : preset.schemes)
            {
                schemes.emplace_back(s);
            }
            rule.Schemes(winrt::single_threaded_vector<winrt::hstring>(std::move(schemes)));
        }
        rule.FileTypeGroup(preset.fileTypeGroup);
        if (!preset.customExtensions.empty())
        {
            std::vector<winrt::hstring> exts;
            for (const auto& e : preset.customExtensions)
            {
                exts.emplace_back(e);
            }
            rule.CustomExtensions(winrt::single_threaded_vector<winrt::hstring>(std::move(exts)));
        }
        rule.Integration(winrt::hstring{ preset.integration });
        rule.ShowPreview(preset.showPreview);
        rule.CustomActions(winrt::single_threaded_vector<Model::HyperlinkTooltipAction>());
        return rule;
    }
}
