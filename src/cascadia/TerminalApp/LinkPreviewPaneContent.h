// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "LinkPreviewPaneContent.g.h"
#include "BasicPaneEvents.h"

namespace winrt::TerminalApp::implementation
{
    struct LinkPreviewPaneContent : LinkPreviewPaneContentT<LinkPreviewPaneContent>, BasicPaneEvents
    {
    public:
        LinkPreviewPaneContent();

        void SetPreviewProvider(const winrt::Microsoft::Terminal::Control::IHyperlinkPreviewProvider& provider);
        void ShowLink(const winrt::hstring& text, const winrt::hstring& integrationHint);

        bool HideTooltips() const noexcept { return _hideTooltips; }
        til::typed_event<winrt::Windows::Foundation::IInspectable, winrt::Windows::Foundation::IInspectable> HideTooltipsChanged;

#pragma region IPaneContent
        winrt::Windows::UI::Xaml::FrameworkElement GetRoot();

        void UpdateSettings(const winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings&,
                            const winrt::Microsoft::Terminal::Settings::Model::WindowSettings&) {};

        winrt::Windows::Foundation::Size MinimumSize() { return { 1, 1 }; };
        void Focus(winrt::Windows::UI::Xaml::FocusState reason = winrt::Windows::UI::Xaml::FocusState::Programmatic);
        void Close();
        winrt::Microsoft::Terminal::Settings::Model::INewContentArgs GetNewTerminalArgs(BuildStartupKind kind) const;

        winrt::hstring Title() { return _title; }
        uint64_t TaskbarState() { return 0; }
        uint64_t TaskbarProgress() { return 0; }
        bool ReadOnly() { return false; }
        winrt::hstring Icon() const;
        winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color> TabColor() const noexcept { return nullptr; }
        winrt::Windows::UI::Xaml::Media::Brush BackgroundBrush() { return Background(); }

        // See BasicPaneEvents for most generic event definitions
#pragma endregion

    private:
        friend struct LinkPreviewPaneContentT<LinkPreviewPaneContent>; // for Xaml to bind events

        winrt::Microsoft::Terminal::Control::IHyperlinkPreviewProvider _provider{ nullptr };
        winrt::Microsoft::Terminal::Control::HyperlinkPreview _preview{ nullptr };

        // What this pane is showing, echoed back into every refresh and action so
        // they run against the same thing the first fetch did.
        winrt::hstring _sourceText;
        winrt::hstring _integrationHint;
        winrt::hstring _title;

        // A pane outlives any one fetch, and a second ShowLink can arrive while the
        // first is still in flight. Same guard the card uses, for the same reason.
        uint32_t _generation{ 0 };
        bool _hideTooltips{ false };

        // -1 is the field list, 0.. index into the preview's own Tabs.
        int32_t _selectedTab{ -1 };

        winrt::Microsoft::Terminal::Control::HyperlinkPreviewAction _action{ nullptr };
        winrt::hstring _undoChoiceId;
        winrt::Windows::Foundation::IInspectable _undoDefaultContent{ nullptr };

        safe_void_coroutine _fetch(uint32_t generation, bool refresh);
        safe_void_coroutine _invokeAction(uint32_t generation, winrt::hstring choiceId);

        void _setLoading(bool loading);
        void _render(const winrt::Microsoft::Terminal::Control::HyperlinkPreview& preview);
        void _renderFields(const winrt::Microsoft::Terminal::Control::HyperlinkPreview& preview);
        void _renderBody(const winrt::Microsoft::Terminal::Control::HyperlinkPreviewTab& tab);
        // Shared by Body tabs and by each comment in a Comments tab: both carry text
        // whose formatting is declared by the tab, not by the item.
        winrt::Windows::UI::Xaml::UIElement _makeBodyElement(const winrt::hstring& body, const winrt::hstring& format);
        void _renderComments(const winrt::Microsoft::Terminal::Control::HyperlinkPreviewTab& tab);
        void _rebuildTabStrip(const winrt::Microsoft::Terminal::Control::HyperlinkPreview& preview);
        void _showTab(int32_t index);
        void _rebuildActions(const winrt::Microsoft::Terminal::Control::HyperlinkPreview& preview);
        void _updateActionFields();
        void _updateApplyState();
        void _setActionBusy(bool busy);
        winrt::Microsoft::Terminal::Control::HyperlinkPreviewActionOption _selectedOption();
        winrt::Windows::Foundation::Collections::IMap<winrt::hstring, winrt::hstring> _collectFieldValues();
        void _setError(const winrt::hstring& message);

        winrt::Windows::UI::Xaml::UIElement _makeFieldRows(const std::vector<winrt::Microsoft::Terminal::Control::HyperlinkPreviewField>& fields);

        void _tabClick(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _closeClick(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _refreshClick(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _applyClick(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _undoClick(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
        void _optionChanged(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::Controls::SelectionChangedEventArgs& e);
        void _hideTooltipsToggled(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& e);
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(LinkPreviewPaneContent);
}
