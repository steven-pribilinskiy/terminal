// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "IntegrationSettings.h"
#include "JsonUtils.h"

#include "IntegrationSettings.g.cpp"

using namespace Microsoft::Terminal::Settings::Model;
using namespace winrt::Windows::Foundation::Collections;

static constexpr std::string_view EnabledKey{ "enabled" };
static constexpr std::string_view ValuesKey{ "settings" };
static constexpr std::string_view FieldsKey{ "fields" };
static constexpr std::string_view TabsKey{ "tabs" };

namespace winrt::Microsoft::Terminal::Settings::Model::implementation
{
    Json::Value IntegrationSettings::ToJson() const
    {
        Json::Value json{ Json::ValueType::objectValue };
        JsonUtils::SetValueForKey(json, EnabledKey, _Enabled);
        if (_Values && _Values.Size() > 0)
        {
            JsonUtils::SetValueForKey(json, ValuesKey, _Values);
        }
        JsonUtils::SetValueForKey(json, FieldsKey, _Fields);
        JsonUtils::SetValueForKey(json, TabsKey, _Tabs);
        return json;
    }

    winrt::com_ptr<IntegrationSettings> IntegrationSettings::FromJson(const Json::Value& json)
    {
        auto settings = winrt::make_self<IntegrationSettings>();
        JsonUtils::GetValueForKey(json, EnabledKey, settings->_Enabled);
        JsonUtils::GetValueForKey(json, ValuesKey, settings->_Values);
        JsonUtils::GetValueForKey(json, FieldsKey, settings->_Fields);
        JsonUtils::GetValueForKey(json, TabsKey, settings->_Tabs);
        return settings;
    }

    Model::IntegrationSettings IntegrationSettings::Copy() const
    {
        auto settings = winrt::make_self<IntegrationSettings>();
        settings->_Enabled = _Enabled;
        if (_Values)
        {
            settings->_Values = winrt::single_threaded_map<hstring, hstring>();
            for (const auto& [key, value] : _Values)
            {
                settings->_Values.Insert(key, value);
            }
        }
        if (_Fields)
        {
            settings->_Fields = winrt::single_threaded_vector<hstring>();
            for (const auto& field : _Fields)
            {
                settings->_Fields.Append(field);
            }
        }
        if (_Tabs)
        {
            settings->_Tabs = winrt::single_threaded_vector<hstring>();
            for (const auto& tab : _Tabs)
            {
                settings->_Tabs.Append(tab);
            }
        }
        return *settings;
    }
}
