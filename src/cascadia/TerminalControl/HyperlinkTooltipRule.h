/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- HyperlinkTooltipRule.h

Abstract:
- Control-layer mirror of Microsoft.Terminal.Settings.Model.HyperlinkTooltipRule
  and HyperlinkTooltipAction. TerminalSettings (the App-adapter layer) translates
  the Settings Model list into these; TermControl never touches the Model types
  directly, consistent with the rest of IControlSettings.

--*/
#pragma once

#include "HyperlinkTooltipAction.g.h"
#include "HyperlinkTooltipRule.g.h"

namespace winrt::Microsoft::Terminal::Control::implementation
{
    struct HyperlinkTooltipAction : HyperlinkTooltipActionT<HyperlinkTooltipAction>
    {
        HyperlinkTooltipAction() = default;

        WINRT_PROPERTY(hstring, Name);
        WINRT_PROPERTY(hstring, ActionId);
        WINRT_PROPERTY(hstring, Icon);
    };

    struct HyperlinkTooltipRule : HyperlinkTooltipRuleT<HyperlinkTooltipRule>
    {
        HyperlinkTooltipRule() = default;

        WINRT_PROPERTY(hstring, Name);
        WINRT_PROPERTY(bool, Enabled, true);
        WINRT_PROPERTY(HyperlinkMatchKind, Kind, HyperlinkMatchKind::Link);

        WINRT_PROPERTY(Windows::Foundation::Collections::IVector<hstring>, Schemes);
        WINRT_PROPERTY(hstring, Pattern);
        WINRT_PROPERTY(HyperlinkFileTypeGroup, FileTypeGroup, HyperlinkFileTypeGroup::None);
        WINRT_PROPERTY(Windows::Foundation::Collections::IVector<hstring>, CustomExtensions);

        WINRT_PROPERTY(hstring, Integration);
        WINRT_PROPERTY(bool, ShowPreview, true);

        WINRT_PROPERTY(Windows::Foundation::IReference<int32_t>, TooltipShowDelay);
        WINRT_PROPERTY(Windows::Foundation::IReference<int32_t>, TooltipHideDelay);
        WINRT_PROPERTY(Windows::Foundation::IReference<int32_t>, TooltipMaxWidth);

        WINRT_PROPERTY(Windows::Foundation::Collections::IVector<hstring>, Buttons);
        WINRT_PROPERTY(Windows::Foundation::IReference<bool>, ShowInPane);

        WINRT_PROPERTY(Windows::Foundation::Collections::IVector<Control::HyperlinkTooltipAction>, CustomActions);
    };
}

namespace winrt::Microsoft::Terminal::Control::factory_implementation
{
    BASIC_FACTORY(HyperlinkTooltipAction);
    BASIC_FACTORY(HyperlinkTooltipRule);
}
