// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "HyperlinkTooltipAction.h"
#include "JsonUtils.h"
#include "TerminalSettingsSerializationHelpers.h"

#include "HyperlinkTooltipAction.g.cpp"

using namespace Microsoft::Terminal::Settings::Model;

static constexpr std::string_view NameKey{ "name" };
static constexpr std::string_view ActionIdKey{ "id" };
static constexpr std::string_view IconKey{ "icon" };

namespace winrt::Microsoft::Terminal::Settings::Model::implementation
{
    Json::Value HyperlinkTooltipAction::ToJson() const
    {
        Json::Value json{ Json::ValueType::objectValue };

        JsonUtils::SetValueForKey(json, NameKey, _Name);
        JsonUtils::SetValueForKey(json, ActionIdKey, _ActionId);
        JsonUtils::SetValueForKey(json, IconKey, _icon);

        return json;
    }

    winrt::com_ptr<HyperlinkTooltipAction> HyperlinkTooltipAction::FromJson(const Json::Value& json)
    {
        auto action = winrt::make_self<HyperlinkTooltipAction>();

        JsonUtils::GetValueForKey(json, NameKey, action->_Name);
        JsonUtils::GetValueForKey(json, ActionIdKey, action->_ActionId);
        JsonUtils::GetValueForKey(json, IconKey, action->_icon);

        return action;
    }

    Model::HyperlinkTooltipAction HyperlinkTooltipAction::Copy() const
    {
        auto action = winrt::make_self<HyperlinkTooltipAction>();
        action->_Name = _Name;
        action->_ActionId = _ActionId;
        action->_icon = _icon;
        return *action;
    }

    void HyperlinkTooltipAction::ResolveMediaResourcesWithBasePath(const winrt::hstring& basePath, const Model::MediaResourceResolver& resolver)
    {
        if (_icon)
        {
            // TODO GH#19191 (Hardcoded Origin, since that's the only place it could have come from)
            ResolveIconMediaResource(OriginTag::User, basePath, _icon, resolver);
        }
    }
}
