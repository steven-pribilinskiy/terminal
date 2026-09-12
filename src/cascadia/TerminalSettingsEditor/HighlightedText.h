/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- HighlightedText

Abstract:
- Draws a string with some of its characters emphasised, from the match runs a
  fuzzy matcher produced. The settings editor's counterpart to TerminalApp's
  HighlightedTextControl.

--*/

#pragma once

#include "HighlightedText.g.h"
#include "Utils.h"

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    struct HighlightedText : HighlightedTextT<HighlightedText>
    {
    public:
        HighlightedText();

        DEPENDENCY_PROPERTY(hstring, Text);
        DEPENDENCY_PROPERTY(Windows::Foundation::Collections::IVector<Editor::HighlightedTextRun>, Runs);

        // DEPENDENCY_PROPERTY ends in `private:` (that is where it declares the
        // backing static), so everything below is private already - said out loud
        // because the next person to add a public member here will need `public:`.
    private:
        static void _InitializeProperties();
        static void _OnTextOrRunsChanged(const Windows::UI::Xaml::DependencyObject& d, const Windows::UI::Xaml::DependencyPropertyChangedEventArgs& e);

        void _UpdateInlines();
    };
}

namespace winrt::Microsoft::Terminal::Settings::Editor::factory_implementation
{
    BASIC_FACTORY(HighlightedText);
}
