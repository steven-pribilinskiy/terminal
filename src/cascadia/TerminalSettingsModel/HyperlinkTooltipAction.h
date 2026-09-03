/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- HyperlinkTooltipAction.h

Abstract:
- A custom action button shown on the hyperlink tooltip card for links
  matched by a HyperlinkTooltipRule.

--*/
#pragma once

#include "HyperlinkTooltipAction.g.h"
#include "MediaResourceSupport.h"

namespace winrt::Microsoft::Terminal::Settings::Model::implementation
{
    struct HyperlinkTooltipAction : HyperlinkTooltipActionT<HyperlinkTooltipAction, IPathlessMediaResourceContainer>
    {
    public:
        HyperlinkTooltipAction() noexcept = default;

        Model::HyperlinkTooltipAction Copy() const;

        Json::Value ToJson() const;
        static com_ptr<HyperlinkTooltipAction> FromJson(const Json::Value& json);

        void ResolveMediaResourcesWithBasePath(const winrt::hstring& basePath, const Model::MediaResourceResolver& resolver) override;

        IMediaResource Icon() const noexcept
        {
            return _icon ? _icon : MediaResource::Empty();
        }

        void Icon(const IMediaResource& val)
        {
            _icon = val;
        }

        WINRT_PROPERTY(winrt::hstring, Name);
        WINRT_PROPERTY(winrt::hstring, ActionId);

    private:
        IMediaResource _icon;
    };
}

namespace winrt::Microsoft::Terminal::Settings::Model::factory_implementation
{
    BASIC_FACTORY(HyperlinkTooltipAction);
}

namespace Microsoft::Terminal::Settings::Model::JsonUtils
{
    using namespace winrt::Microsoft::Terminal::Settings::Model;

    template<>
    struct ConversionTrait<HyperlinkTooltipAction>
    {
        HyperlinkTooltipAction FromJson(const Json::Value& json)
        {
            return *implementation::HyperlinkTooltipAction::FromJson(json);
        }

        bool CanConvert(const Json::Value& json) const
        {
            return json.isObject();
        }

        Json::Value ToJson(const HyperlinkTooltipAction& val)
        {
            return winrt::get_self<implementation::HyperlinkTooltipAction>(val)->ToJson();
        }

        std::string TypeDescription() const
        {
            return "HyperlinkTooltipAction";
        }
    };
}
