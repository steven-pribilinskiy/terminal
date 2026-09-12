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
    // Reads one of the two shapes this key has had.
    //
    // The object -- { "updated": false } -- is what we write: one entry per
    // decision the user actually made, with everything else left to the
    // manifest's default.
    //
    // The array -- [ "summary", "status" ] -- is what every settings.json written
    // before that carries: the complete set of keys to show, seeded from the
    // manifest's defaults on the first tick. Its absences mean two different
    // things at once (turned off, or added to the manifest since) and there is
    // nothing in the file to tell them apart, so only what it names is carried
    // over. A field the user had hidden therefore comes back once, at its
    // default, which is the lesser of the two errors: a key recorded as declined
    // when it was merely unheard-of can never appear at all.
    static void ParseOverrides(const Json::Value& json, std::string_view key, IntegrationOverrideMap& target)
    {
        const auto value = json.find(key.data(), key.data() + key.size());
        if (!value || value->isNull())
        {
            return;
        }

        if (value->isObject())
        {
            JsonUtils::GetValue(*value, target);
            return;
        }

        if (!value->isArray())
        {
            return;
        }

        auto overrides = winrt::single_threaded_map<hstring, bool>();
        for (const auto& entry : *value)
        {
            if (entry.isString())
            {
                overrides.Insert(winrt::to_hstring(entry.asString()), true);
            }
        }
        target = std::move(overrides);
    }

    Json::Value IntegrationSettings::ToJson() const
    {
        Json::Value json{ Json::ValueType::objectValue };
        JsonUtils::SetValueForKey(json, EnabledKey, _Enabled);
        if (_Values && _Values.Size() > 0)
        {
            JsonUtils::SetValueForKey(json, ValuesKey, _Values);
        }
        // An empty map and no map at all mean the same thing now -- every key is
        // at its manifest default -- so the empty one is not written. That is
        // also what lets _pruneEmptyEntry recognise an entry holding nothing.
        if (_Fields && _Fields.Size() > 0)
        {
            JsonUtils::SetValueForKey(json, FieldsKey, _Fields);
        }
        if (_Tabs && _Tabs.Size() > 0)
        {
            JsonUtils::SetValueForKey(json, TabsKey, _Tabs);
        }
        return json;
    }

    winrt::com_ptr<IntegrationSettings> IntegrationSettings::FromJson(const Json::Value& json)
    {
        auto settings = winrt::make_self<IntegrationSettings>();
        JsonUtils::GetValueForKey(json, EnabledKey, settings->_Enabled);
        JsonUtils::GetValueForKey(json, ValuesKey, settings->_Values);
        ParseOverrides(json, FieldsKey, settings->_Fields);
        ParseOverrides(json, TabsKey, settings->_Tabs);
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
            settings->_Fields = winrt::single_threaded_map<hstring, bool>();
            for (const auto& [key, visible] : _Fields)
            {
                settings->_Fields.Insert(key, visible);
            }
        }
        if (_Tabs)
        {
            settings->_Tabs = winrt::single_threaded_map<hstring, bool>();
            for (const auto& [key, visible] : _Tabs)
            {
                settings->_Tabs.Insert(key, visible);
            }
        }
        return *settings;
    }
}
