// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "HyperlinkTooltipRule.h"
#include "JsonUtils.h"
#include "TerminalSettingsSerializationHelpers.h"

#include "HyperlinkTooltipRule.g.cpp"

using namespace Microsoft::Terminal::Settings::Model;
using namespace winrt::Windows::Foundation::Collections;

static constexpr std::string_view NameKey{ "name" };
static constexpr std::string_view EnabledKey{ "enabled" };
static constexpr std::string_view SchemesKey{ "schemes" };
static constexpr std::string_view PatternKey{ "pattern" };
static constexpr std::string_view FileTypeGroupKey{ "fileTypeGroup" };
static constexpr std::string_view CustomExtensionsKey{ "extensions" };
static constexpr std::string_view ShowDelayKey{ "showDelay" };
static constexpr std::string_view HideDelayKey{ "hideDelay" };
static constexpr std::string_view MaxWidthKey{ "maxWidth" };
static constexpr std::string_view SuppressOpenKey{ "suppressOpen" };
static constexpr std::string_view SuppressCopyLinkKey{ "suppressCopyLink" };
static constexpr std::string_view SuppressCopyPathKey{ "suppressCopyPath" };
static constexpr std::string_view SuppressRevealKey{ "suppressReveal" };
static constexpr std::string_view CustomActionsKey{ "actions" };

namespace winrt::Microsoft::Terminal::Settings::Model::implementation
{
    Json::Value HyperlinkTooltipRule::ToJson() const
    {
        Json::Value json{ Json::ValueType::objectValue };

        JsonUtils::SetValueForKey(json, NameKey, _Name);
        JsonUtils::SetValueForKey(json, EnabledKey, _Enabled);
        JsonUtils::SetValueForKey(json, SchemesKey, _Schemes);
        JsonUtils::SetValueForKey(json, PatternKey, _Pattern);
        JsonUtils::SetValueForKey(json, FileTypeGroupKey, _FileTypeGroup);
        JsonUtils::SetValueForKey(json, CustomExtensionsKey, _CustomExtensions);
        JsonUtils::SetValueForKey(json, ShowDelayKey, _TooltipShowDelay);
        JsonUtils::SetValueForKey(json, HideDelayKey, _TooltipHideDelay);
        JsonUtils::SetValueForKey(json, MaxWidthKey, _TooltipMaxWidth);
        JsonUtils::SetValueForKey(json, SuppressOpenKey, _SuppressOpen);
        JsonUtils::SetValueForKey(json, SuppressCopyLinkKey, _SuppressCopyLink);
        JsonUtils::SetValueForKey(json, SuppressCopyPathKey, _SuppressCopyPath);
        JsonUtils::SetValueForKey(json, SuppressRevealKey, _SuppressReveal);
        JsonUtils::SetValueForKey(json, CustomActionsKey, _CustomActions);

        return json;
    }

    winrt::com_ptr<HyperlinkTooltipRule> HyperlinkTooltipRule::FromJson(const Json::Value& json)
    {
        auto rule = winrt::make_self<HyperlinkTooltipRule>();

        JsonUtils::GetValueForKey(json, NameKey, rule->_Name);
        JsonUtils::GetValueForKey(json, EnabledKey, rule->_Enabled);
        JsonUtils::GetValueForKey(json, SchemesKey, rule->_Schemes);
        JsonUtils::GetValueForKey(json, PatternKey, rule->_Pattern);
        JsonUtils::GetValueForKey(json, FileTypeGroupKey, rule->_FileTypeGroup);
        JsonUtils::GetValueForKey(json, CustomExtensionsKey, rule->_CustomExtensions);
        JsonUtils::GetValueForKey(json, ShowDelayKey, rule->_TooltipShowDelay);
        JsonUtils::GetValueForKey(json, HideDelayKey, rule->_TooltipHideDelay);
        JsonUtils::GetValueForKey(json, MaxWidthKey, rule->_TooltipMaxWidth);
        JsonUtils::GetValueForKey(json, SuppressOpenKey, rule->_SuppressOpen);
        JsonUtils::GetValueForKey(json, SuppressCopyLinkKey, rule->_SuppressCopyLink);
        JsonUtils::GetValueForKey(json, SuppressCopyPathKey, rule->_SuppressCopyPath);
        JsonUtils::GetValueForKey(json, SuppressRevealKey, rule->_SuppressReveal);
        JsonUtils::GetValueForKey(json, CustomActionsKey, rule->_CustomActions);

        return rule;
    }

    Model::HyperlinkTooltipRule HyperlinkTooltipRule::Copy() const
    {
        auto rule = winrt::make_self<HyperlinkTooltipRule>();
        rule->_Name = _Name;
        rule->_Enabled = _Enabled;
        rule->_Pattern = _Pattern;
        rule->_FileTypeGroup = _FileTypeGroup;
        rule->_TooltipShowDelay = _TooltipShowDelay;
        rule->_TooltipHideDelay = _TooltipHideDelay;
        rule->_TooltipMaxWidth = _TooltipMaxWidth;
        rule->_SuppressOpen = _SuppressOpen;
        rule->_SuppressCopyLink = _SuppressCopyLink;
        rule->_SuppressCopyPath = _SuppressCopyPath;
        rule->_SuppressReveal = _SuppressReveal;

        if (_Schemes)
        {
            rule->_Schemes = winrt::single_threaded_vector<winrt::hstring>();
            for (const auto& s : _Schemes)
            {
                rule->_Schemes.Append(s);
            }
        }
        if (_CustomExtensions)
        {
            rule->_CustomExtensions = winrt::single_threaded_vector<winrt::hstring>();
            for (const auto& e : _CustomExtensions)
            {
                rule->_CustomExtensions.Append(e);
            }
        }
        if (_CustomActions)
        {
            rule->_CustomActions = winrt::single_threaded_vector<Model::HyperlinkTooltipAction>();
            for (const auto& a : _CustomActions)
            {
                rule->_CustomActions.Append(get_self<HyperlinkTooltipAction>(a)->Copy());
            }
        }

        return *rule;
    }
}
