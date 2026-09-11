// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "Builder.g.h"

namespace winrt::Microsoft::Terminal::UI::Markdown::implementation
{
    struct Builder
    {
        static winrt::Windows::UI::Xaml::Controls::RichTextBlock Convert(const winrt::hstring& text, const winrt::hstring& baseUrl);
        static winrt::Windows::UI::Xaml::FrameworkElement Preview(const winrt::hstring& text, const winrt::hstring& baseUrl, bool raw);
        static winrt::Windows::UI::Xaml::Controls::TextBlock Highlight(const winrt::hstring& text, const winrt::hstring& language);
    };
}

namespace winrt::Microsoft::Terminal::UI::Markdown::factory_implementation
{
    BASIC_FACTORY(Builder);
}
