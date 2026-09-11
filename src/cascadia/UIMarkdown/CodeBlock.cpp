// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "CodeBlock.h"
#include "MarkdownPresentation.h"
#include <winrt/Windows.ApplicationModel.DataTransfer.h>

#include "CodeBlock.g.cpp"
#include "RequestRunCommandsArgs.g.cpp"

namespace winrt
{
    namespace MUX = Microsoft::UI::Xaml;
    namespace WUX = Windows::UI::Xaml;
    using IInspectable = Windows::Foundation::IInspectable;
}

namespace winrt::Microsoft::Terminal::UI::Markdown::implementation
{
    CodeBlock::CodeBlock(const winrt::hstring& initialCommandlines, const winrt::hstring& language) :
        Commandlines(initialCommandlines)
    {
        InitializeComponent();
        Loaded([weak = get_weak(), language](auto&&, auto&&) {
            try
            {
                if (const auto self = weak.get())
                {
                    self->_codeText = MarkdownPresentation::Code(self->Commandlines(), language);
                    self->_codeText.TextWrapping(self->_wrapped ? WUX::TextWrapping::Wrap : WUX::TextWrapping::NoWrap);
                    self->CommandsAndOutput().Children().Clear();
                    self->CommandsAndOutput().Children().Append(self->_codeText);
                    self->_updateWrapButton();
                }
            }
            CATCH_LOG();
        });
        CodeSurface().PointerEntered([weak = get_weak()](auto&&, auto&&) { if (const auto self = weak.get()) self->CodeActions().Opacity(1); });
        CodeSurface().PointerExited([weak = get_weak()](auto&&, auto&&) { if (const auto self = weak.get()) self->CodeActions().Opacity(0); });
        CodeSurface().GotFocus([weak = get_weak()](auto&&, auto&&) { if (const auto self = weak.get()) self->CodeActions().Opacity(1); });
        CodeSurface().LostFocus([weak = get_weak()](auto&&, auto&&) { if (const auto self = weak.get()) self->CodeActions().Opacity(0); });
        CodeScroll().SizeChanged([weak = get_weak()](auto&&, auto&&) { if (const auto self = weak.get()) self->_updateWrapButton(); });
        CodeScroll().ViewChanged([weak = get_weak()](auto&&, auto&&) { if (const auto self = weak.get()) self->_updateWrapButton(); });
    }

    void CodeBlock::_updateWrapButton()
    {
        WrapButton().Visibility(_wrapped || CodeScroll().ScrollableWidth() > 0.5 ? WUX::Visibility::Visible : WUX::Visibility::Collapsed);
    }

    void CodeBlock::_copyClicked(const Windows::Foundation::IInspectable&, const WUX::RoutedEventArgs&)
    {
        try
        {
            Windows::ApplicationModel::DataTransfer::DataPackage content;
            content.SetText(Commandlines());
            Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(content);
        }
        CATCH_LOG();
    }

    void CodeBlock::_wrapClicked(const Windows::Foundation::IInspectable&, const WUX::RoutedEventArgs&)
    {
        if (!_codeText) return;
        _wrapped = !_wrapped;
        _codeText.TextWrapping(_wrapped ? WUX::TextWrapping::Wrap : WUX::TextWrapping::NoWrap);
        CodeScroll().HorizontalScrollMode(_wrapped ? WUX::Controls::ScrollMode::Disabled : WUX::Controls::ScrollMode::Auto);
        CodeScroll().HorizontalScrollBarVisibility(_wrapped ? WUX::Controls::ScrollBarVisibility::Disabled : WUX::Controls::ScrollBarVisibility::Auto);
        WrapButton().IsChecked(_wrapped);
        WUX::Controls::ToolTipService::SetToolTip(WrapButton(), winrt::box_value(_wrapped ? L"Turn off wrap" : L"Turn on wrap"));
        _updateWrapButton();
    }
    void CodeBlock::_playPressed(const Windows::Foundation::IInspectable&,
                                 const Windows::UI::Xaml::Input::TappedRoutedEventArgs& e)
    {
        auto args = winrt::make_self<RequestRunCommandsArgs>(Commandlines());
        RequestRunCommands.raise(*this, *args);
        e.Handled(true);
    }
}
