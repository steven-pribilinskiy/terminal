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

    #include "LinkTooltipPresets.g.h"

    inline const LinkTooltipPreset* FindLinkTooltipPreset(std::wstring_view id) noexcept
    {
        if (id == L"jira-links") id = L"jira-issue-links";
        if (id == L"github-prs-issues") id = L"github-pull-requests";
        if (id == L"stith-sessions") id = L"stith-session-uris";
        if (id == L"media-preview") id = L"media-files";
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
