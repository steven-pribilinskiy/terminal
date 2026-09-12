// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "HighlightedText.h"
#include "HighlightedText.g.cpp"

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::UI::Text;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Documents;

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    DependencyProperty HighlightedText::_TextProperty{ nullptr };
    DependencyProperty HighlightedText::_RunsProperty{ nullptr };

    HighlightedText::HighlightedText()
    {
        InitializeComponent();
        _InitializeProperties();
    }

    void HighlightedText::_InitializeProperties()
    {
        if (!_TextProperty)
        {
            _TextProperty = DependencyProperty::Register(
                L"Text",
                xaml_typename<hstring>(),
                xaml_typename<Editor::HighlightedText>(),
                PropertyMetadata{ nullptr, PropertyChangedCallback{ &HighlightedText::_OnTextOrRunsChanged } });
        }
        if (!_RunsProperty)
        {
            _RunsProperty = DependencyProperty::Register(
                L"Runs",
                xaml_typename<IVector<Editor::HighlightedTextRun>>(),
                xaml_typename<Editor::HighlightedText>(),
                PropertyMetadata{ nullptr, PropertyChangedCallback{ &HighlightedText::_OnTextOrRunsChanged } });
        }
    }

    void HighlightedText::_OnTextOrRunsChanged(const DependencyObject& d, const DependencyPropertyChangedEventArgs& /*e*/)
    {
        if (const auto control{ d.try_as<Editor::HighlightedText>() })
        {
            get_self<HighlightedText>(control)->_UpdateInlines();
        }
    }

    // Rebuilds the TextBlock's inlines: one Run per alternating unmatched / matched
    // stretch. Ported from TerminalApp's HighlightedTextControl, including the reason
    // every segment is copied into its own hstring - hstring must be null-terminated
    // and slicing does not guarantee that, so handing a sliced wstring_view straight
    // to Run::Text() throws whenever the slice is not at the end of the string.
    //
    // Emphasis is weight only, deliberately. A brush would mean naming a resource,
    // and these live inside a DataTemplate in a virtualizing ListView, where a custom
    // resource key is exactly what fails to resolve.
    void HighlightedText::_UpdateInlines()
    {
        const auto textBlock{ TextView() };
        if (!textBlock)
        {
            return;
        }

        const auto text = Text();
        const auto runs = Runs();

        const auto inlines = textBlock.Inlines();
        inlines.Clear();

        if (text.empty())
        {
            return;
        }

        size_t lastPos = 0;
        if (runs)
        {
            for (const auto& [start, end] : runs)
            {
                const auto runStart = static_cast<size_t>(start);
                // End is inclusive, so a run covers [Start, End].
                const auto runEnd = static_cast<size_t>(end) + 1;

                // A run that does not point into this string would slice out of
                // bounds. Bad runs should not be possible - they come from a match
                // against this very text - but they arrive through a dependency
                // property that anything can set, and dropping them is cheaper than
                // the alternative.
                if (runStart >= text.size() || runEnd <= runStart || runStart < lastPos)
                {
                    continue;
                }

                if (runStart > lastPos)
                {
                    Documents::Run plain;
                    plain.Text(hstring{ til::safe_slice_abs(text, lastPos, runStart) });
                    inlines.Append(plain);
                }

                Documents::Run matched;
                matched.Text(hstring{ til::safe_slice_abs(text, runStart, runEnd) });
                matched.FontWeight(FontWeights::Bold());
                inlines.Append(matched);

                lastPos = std::min(runEnd, text.size());
            }
        }

        // Also the whole string when there were no runs at all. Checking lastPos
        // first avoids a needless deep copy in that, the common, case.
        if (lastPos < text.size())
        {
            Documents::Run tail;
            tail.Text(lastPos == 0 ? text : hstring{ til::safe_slice_abs(text, lastPos, SIZE_T_MAX) });
            inlines.Append(tail);
        }
    }
}
