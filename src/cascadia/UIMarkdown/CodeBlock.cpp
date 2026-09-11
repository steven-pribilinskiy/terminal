// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "CodeBlock.h"
#include "MarkdownPresentation.h"

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
        Loaded([weak = get_weak(), language](auto&&, auto&&) {
            if (const auto self = weak.get())
            {
                self->CommandsAndOutput().Children().Clear();
                self->CommandsAndOutput().Children().Append(MarkdownPresentation::Code(self->Commandlines(), language));
            }
        });
    }
    void CodeBlock::_playPressed(const Windows::Foundation::IInspectable&,
                                 const Windows::UI::Xaml::Input::TappedRoutedEventArgs& e)
    {
        auto args = winrt::make_self<RequestRunCommandsArgs>(Commandlines());
        RequestRunCommands.raise(*this, *args);
        e.Handled(true);
    }
}
