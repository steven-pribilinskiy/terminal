/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- HyperlinkPreview.h

Abstract:
- The data an integration produces for a hovered link, plus the x:Bind helpers
  the hyperlink card's preview template uses. See HyperlinkPreview.idl.

--*/
#pragma once

#include "HyperlinkPreviewField.g.h"
#include "HyperlinkPreview.g.h"
#include "HyperlinkPreviewHelpers.g.h"

namespace winrt::Microsoft::Terminal::Control::implementation
{
    struct HyperlinkPreviewField : HyperlinkPreviewFieldT<HyperlinkPreviewField>
    {
        HyperlinkPreviewField() = default;

        WINRT_PROPERTY(hstring, Label);
        WINRT_PROPERTY(hstring, Value);
        WINRT_PROPERTY(HyperlinkPreviewFieldKind, Kind, HyperlinkPreviewFieldKind::Text);
        WINRT_PROPERTY(hstring, IconUri);
        WINRT_PROPERTY(hstring, Color);

    public:
        bool IsTitle() const noexcept { return _Kind == HyperlinkPreviewFieldKind::Title; }
        bool IsRow() const noexcept { return !IsTitle(); }
        bool IsBadge() const noexcept { return _Kind == HyperlinkPreviewFieldKind::Badge; }
        bool IsPlain() const noexcept { return !IsTitle() && !IsBadge(); }
        bool HasIcon() const noexcept { return !_IconUri.empty(); }
    };

    struct HyperlinkPreview : HyperlinkPreviewT<HyperlinkPreview>
    {
        HyperlinkPreview() = default;

        WINRT_PROPERTY(hstring, IntegrationId);
        WINRT_PROPERTY(hstring, IntegrationName);
        WINRT_PROPERTY(hstring, IntegrationIcon);
        WINRT_PROPERTY(Windows::Foundation::Collections::IVector<Control::HyperlinkPreviewField>, Fields);
        WINRT_PROPERTY(hstring, Html);
        WINRT_PROPERTY(hstring, DataJson);
        WINRT_PROPERTY(hstring, Error);
        WINRT_PROPERTY(hstring, ResolvedUri);
    };

    struct HyperlinkPreviewHelpers
    {
        HyperlinkPreviewHelpers() = default;

        static Windows::UI::Xaml::Media::Brush BadgeBrush(const hstring& color);
        static Windows::UI::Xaml::Media::ImageSource ImageFromUri(const hstring& uri);
    };
}

namespace winrt::Microsoft::Terminal::Control::factory_implementation
{
    BASIC_FACTORY(HyperlinkPreviewField);
    BASIC_FACTORY(HyperlinkPreview);

    struct HyperlinkPreviewHelpers : HyperlinkPreviewHelpersT<HyperlinkPreviewHelpers, implementation::HyperlinkPreviewHelpers>
    {
    };
}
