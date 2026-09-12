// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "KeyChordVisual.h"
#include "KeyChordVisual.g.cpp"

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::Foundation;

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    DependencyProperty KeyChordVisual::_KeyChordProperty{ nullptr };
    DependencyProperty KeyChordVisual::_MatchedRunsProperty{ nullptr };

    KeyChordVisual::KeyChordVisual()
    {
        InitializeComponent();
        _InitializeProperties();
    }

    void KeyChordVisual::_InitializeProperties()
    {
        if (!_KeyChordProperty)
        {
            _KeyChordProperty =
                DependencyProperty::Register(
                    L"KeyChord",
                    xaml_typename<Control::KeyChord>(),
                    xaml_typename<Editor::KeyChordVisual>(),
                    PropertyMetadata{ nullptr, PropertyChangedCallback{ &KeyChordVisual::_OnKeyChordChanged } });
        }
        if (!_MatchedRunsProperty)
        {
            // Same callback: the key visuals are rebuilt either way, and which cap is
            // emphasised is decided while they are being built.
            _MatchedRunsProperty =
                DependencyProperty::Register(
                    L"MatchedRuns",
                    xaml_typename<Windows::Foundation::Collections::IVector<Editor::HighlightedTextRun>>(),
                    xaml_typename<Editor::KeyChordVisual>(),
                    PropertyMetadata{ nullptr, PropertyChangedCallback{ &KeyChordVisual::_OnKeyChordChanged } });
        }
    }

    void KeyChordVisual::_OnKeyChordChanged(const DependencyObject& d, const DependencyPropertyChangedEventArgs& /*e*/)
    {
        if (const auto control{ d.try_as<Editor::KeyChordVisual>() })
        {
            const auto controlImpl{ get_self<KeyChordVisual>(control) };
            controlImpl->_UpdateKeyVisuals();
        }
    }

    // Capitalizes the first character of the provided string.
    // Examples: "enter" -> "Enter", "f1" -> "F1", "v" -> "V"
    static winrt::hstring _formatMainKeyName(std::wstring_view part)
    {
        if (part.empty())
        {
            return {};
        }

        std::wstring buffer{ part };
        buffer[0] = til::toupper_ascii(buffer[0]);
        return winrt::hstring{ buffer };
    }

    // True if any match run touches the span [offset, offset + length) that one key cap
    // occupies in the serialized chord.
    //
    // The offsets line up because the runs were matched against
    // KeyChordViewModel::KeyChordText, and that is the same
    // KeyChordSerialization::ToString this control splits below - if those two ever stop
    // being the same string, the emphasis lands on the wrong cap.
    static bool _partIsMatched(const Windows::Foundation::Collections::IVector<Editor::HighlightedTextRun>& runs,
                               const size_t offset,
                               const size_t length)
    {
        if (!runs || length == 0)
        {
            return false;
        }

        const auto partEnd = offset + length; // exclusive
        for (const auto& run : runs)
        {
            // Run::End is inclusive, hence >= rather than >.
            if (static_cast<size_t>(run.Start) < partEnd && static_cast<size_t>(run.End) >= offset)
            {
                return true;
            }
        }
        return false;
    }

    void KeyChordVisual::_UpdateKeyVisuals()
    {
        auto panel{ KeysPanel() };
        if (!panel)
        {
            return;
        }
        panel.Children().Clear();

        const auto kc{ KeyChord() };
        if (!kc)
        {
            return;
        }

        const auto runs{ MatchedRuns() };

        // Reuse the canonical serialization so the key naming stays in sync with the
        // rest of the app. Then split on '+' (no key name in the table contains a literal
        // '+'; VK_OEM_PLUS serializes as "plus") and render each part as its own visual.
        const auto serialized{ Model::KeyChordSerialization::ToString(kc) };
        if (serialized.empty())
        {
            return;
        }

        const std::wstring_view full{ serialized };
        for (const auto part : til::split_iterator{ full, L'+' })
        {
            // Where this part sits in the serialization, so the match runs can be asked
            // about it. The split hands back views into `full`, so the offset is just the
            // distance between the two.
            const auto offset = static_cast<size_t>(part.data() - full.data());
            const auto emphasised = _partIsMatched(runs, offset, part.size());

            if (til::equals_insensitive_ascii(part, L"win"))
            {
                // The Windows cap is a Path, not text, so there is nothing to embolden.
                // A matched "@win" still filters the list correctly; only this one cap
                // goes unmarked.
                _AddGlyphKey();
            }
            else if (til::equals_insensitive_ascii(part, L"ctrl"))
            {
                _AddTextKey(L"Ctrl", emphasised);
            }
            else if (til::equals_insensitive_ascii(part, L"alt"))
            {
                _AddTextKey(L"Alt", emphasised);
            }
            else if (til::equals_insensitive_ascii(part, L"shift"))
            {
                _AddTextKey(L"Shift", emphasised);
            }
            else
            {
                _AddTextKey(_formatMainKeyName(part), emphasised);
            }
        }
    }

    void KeyChordVisual::_AddTextKey(const winrt::hstring& text, const bool emphasised)
    {
        const auto tmpl{ Resources().Lookup(box_value(L"KeyChordVisualTextKeyTemplate")).as<DataTemplate>() };
        const auto border{ tmpl.LoadContent().as<Border>() };
        if (const auto tb{ border.Child().try_as<TextBlock>() })
        {
            tb.Text(text);
            if (emphasised)
            {
                // Weight only, and deliberately no brush. It is the same signal
                // HighlightedText uses for a matched run in a name, so a match reads the
                // same whichever mode found it - and a brush would mean naming a
                // resource, which from inside a DataTemplate a virtualizing ListView
                // instantiated is the thing that does not resolve. (Resources() here is
                // this control's own dictionary and would not find a system brush
                // anyway; it holds the two key cap templates and nothing else.)
                tb.FontWeight(Windows::UI::Text::FontWeights::Bold());
            }
        }
        KeysPanel().Children().Append(border);
    }

    void KeyChordVisual::_AddGlyphKey()
    {
        const auto tmpl{ Resources().Lookup(box_value(L"KeyChordVisualWindowsKeyTemplate")).as<DataTemplate>() };
        const auto border{ tmpl.LoadContent().as<Border>() };

        // Provide an accessible name for the glyph since it has no text fallback.
        if (const auto path{ border.Child() })
        {
            Automation::AutomationProperties::SetName(path, L"Win");
        }
        KeysPanel().Children().Append(border);
    }
}
