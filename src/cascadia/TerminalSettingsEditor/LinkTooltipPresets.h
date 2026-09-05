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
