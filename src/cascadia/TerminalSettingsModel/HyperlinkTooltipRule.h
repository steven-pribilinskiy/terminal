/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- HyperlinkTooltipRule.h

Abstract:
- A rule that customizes the hyperlink tooltip card's behavior (show/hide
  delay, max width, which buttons appear) for links matching its criteria.

--*/
#pragma once

#include "HyperlinkTooltipRule.g.h"
#include "HyperlinkTooltipAction.h"
#include "JsonUtils.h"

namespace winrt::Microsoft::Terminal::Settings::Model::implementation
{
    struct HyperlinkTooltipRule : HyperlinkTooltipRuleT<HyperlinkTooltipRule>
    {
    public:
        HyperlinkTooltipRule() noexcept = default;

        Model::HyperlinkTooltipRule Copy() const;

        Json::Value ToJson() const;
        static com_ptr<HyperlinkTooltipRule> FromJson(const Json::Value& json);

        WINRT_PROPERTY(winrt::hstring, Name);
        WINRT_PROPERTY(bool, Enabled, true);
        WINRT_PROPERTY(Model::HyperlinkMatchKind, Kind, Model::HyperlinkMatchKind::Link);

        WINRT_PROPERTY(winrt::Windows::Foundation::Collections::IVector<winrt::hstring>, Schemes);
        WINRT_PROPERTY(winrt::hstring, Pattern);
        WINRT_PROPERTY(Model::HyperlinkFileTypeGroup, FileTypeGroup, Model::HyperlinkFileTypeGroup::None);
        WINRT_PROPERTY(winrt::Windows::Foundation::Collections::IVector<winrt::hstring>, CustomExtensions);

        WINRT_PROPERTY(winrt::hstring, Integration);
        WINRT_PROPERTY(bool, ShowPreview, true);

        WINRT_PROPERTY(winrt::Windows::Foundation::IReference<int32_t>, TooltipShowDelay);
        WINRT_PROPERTY(winrt::Windows::Foundation::IReference<int32_t>, TooltipHideDelay);
        WINRT_PROPERTY(winrt::Windows::Foundation::IReference<int32_t>, TooltipMaxWidth);

        WINRT_PROPERTY(winrt::Windows::Foundation::Collections::IVector<winrt::hstring>, Buttons);
        WINRT_PROPERTY(winrt::Windows::Foundation::IReference<bool>, ShowInPane);

        WINRT_PROPERTY(winrt::hstring, PrimaryAction);
        WINRT_PROPERTY(winrt::hstring, AlternativeAction);

        WINRT_PROPERTY(winrt::Windows::Foundation::Collections::IVector<Model::HyperlinkTooltipAction>, CustomActions);
    };
}

namespace winrt::Microsoft::Terminal::Settings::Model::factory_implementation
{
    BASIC_FACTORY(HyperlinkTooltipRule);
}

namespace Microsoft::Terminal::Settings::Model::JsonUtils
{
    using namespace winrt::Microsoft::Terminal::Settings::Model;

    template<>
    struct ConversionTrait<HyperlinkTooltipRule>
    {
        HyperlinkTooltipRule FromJson(const Json::Value& json)
        {
            return *implementation::HyperlinkTooltipRule::FromJson(json);
        }

        bool CanConvert(const Json::Value& json) const
        {
            return json.isObject();
        }

        Json::Value ToJson(const HyperlinkTooltipRule& val)
        {
            return winrt::get_self<implementation::HyperlinkTooltipRule>(val)->ToJson();
        }

        std::string TypeDescription() const
        {
            return "HyperlinkTooltipRule";
        }
    };
}
