/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- IntegrationSettings.h

Abstract:
- The user's configuration of one integration (enabled, non-secret setting
  values, which display fields to show), persisted under "integrations" in
  settings.json.

--*/
#pragma once

#include "IntegrationSettings.g.h"
#include "JsonUtils.h"

namespace winrt::Microsoft::Terminal::Settings::Model::implementation
{
    // The comma in IMap<K, V> would split WINRT_PROPERTY's macro arguments.
    using IntegrationValueMap = Windows::Foundation::Collections::IMap<hstring, hstring>;

    struct IntegrationSettings : IntegrationSettingsT<IntegrationSettings>
    {
    public:
        IntegrationSettings() noexcept = default;

        Model::IntegrationSettings Copy() const;

        Json::Value ToJson() const;
        static com_ptr<IntegrationSettings> FromJson(const Json::Value& json);

        WINRT_PROPERTY(bool, Enabled, false);
        WINRT_PROPERTY(IntegrationValueMap, Values);
        WINRT_PROPERTY(Windows::Foundation::Collections::IVector<hstring>, Fields);
        WINRT_PROPERTY(Windows::Foundation::Collections::IVector<hstring>, Tabs);
    };
}

namespace winrt::Microsoft::Terminal::Settings::Model::factory_implementation
{
    BASIC_FACTORY(IntegrationSettings);
}

namespace Microsoft::Terminal::Settings::Model::JsonUtils
{
    using namespace winrt::Microsoft::Terminal::Settings::Model;

    template<>
    struct ConversionTrait<IntegrationSettings>
    {
        IntegrationSettings FromJson(const Json::Value& json)
        {
            return *implementation::IntegrationSettings::FromJson(json);
        }

        bool CanConvert(const Json::Value& json) const
        {
            return json.isObject();
        }

        Json::Value ToJson(const IntegrationSettings& val)
        {
            return winrt::get_self<implementation::IntegrationSettings>(val)->ToJson();
        }

        std::string TypeDescription() const
        {
            return "IntegrationSettings";
        }
    };
}
