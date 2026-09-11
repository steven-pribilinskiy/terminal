// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "CodeBlock.g.h"
#include "RequestRunCommandsArgs.g.h"
#include "../../../src/cascadia/inc/cppwinrt_utils.h"
#include <til/hash.h>
#include <til/winrt.h>

namespace winrt::Microsoft::Terminal::UI::Markdown::implementation
{
    struct CodeBlock : CodeBlockT<CodeBlock>
    {
        CodeBlock(const winrt::hstring& initialCommandlines, const winrt::hstring& language = L"");

        til::property<winrt::hstring> Commandlines;

        til::property_changed_event PropertyChanged;
        til::typed_event<Microsoft::Terminal::UI::Markdown::CodeBlock, RequestRunCommandsArgs> RequestRunCommands;

        WINRT_OBSERVABLE_PROPERTY(Windows::UI::Xaml::Visibility, PlayButtonVisibility, PropertyChanged.raise, Windows::UI::Xaml::Visibility::Collapsed);

    private:
        friend struct CodeBlockT<CodeBlock>; // for Xaml to bind events

        void _playPressed(const Windows::Foundation::IInspectable& sender, const Windows::UI::Xaml::Input::TappedRoutedEventArgs& e);
        void _copyClicked(const Windows::Foundation::IInspectable&, const Windows::UI::Xaml::RoutedEventArgs&);
        void _wrapClicked(const Windows::Foundation::IInspectable&, const Windows::UI::Xaml::RoutedEventArgs&);
        void _updateWrapButton();
        Windows::UI::Xaml::Controls::TextBlock _codeText{ nullptr };
        bool _wrapped = false;
    };

    struct RequestRunCommandsArgs : RequestRunCommandsArgsT<RequestRunCommandsArgs>
    {
        RequestRunCommandsArgs(const winrt::hstring& commandlines) noexcept :
            Commandlines{ commandlines } {};

        til::property<winrt::hstring> Commandlines;
    };
}

namespace winrt::Microsoft::Terminal::UI::Markdown::factory_implementation
{
    BASIC_FACTORY(CodeBlock);
}
