// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "LinkTooltip.h"
#include "LinkTooltip.g.cpp"

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Navigation;
using namespace winrt::Microsoft::Terminal::Settings::Model;

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    LinkTooltip::LinkTooltip()
    {
        InitializeComponent();
    }

    void LinkTooltip::OnNavigatedTo(const NavigationEventArgs& e)
    {
        const auto args = e.Parameter().as<Editor::NavigateToPageArgs>();
        _ViewModel = args.ViewModel().as<Editor::LinkTooltipViewModel>();
        _weakWindowRoot = args.WindowRoot();
        BringIntoViewWhenLoaded(args.ElementToFocus());

        TraceLoggingWrite(
            g_hTerminalSettingsEditorProvider,
            "NavigatedToPage",
            TraceLoggingDescription("Event emitted when the user navigates to a page in the settings UI"),
            TraceLoggingValue("linkTooltip", "PageId", "The identifier of the page that was navigated to"),
            TraceLoggingKeyword(MICROSOFT_KEYWORD_MEASURES),
            TelemetryPrivacyDataTag(PDT_ProductAndServiceUsage));
    }

    void LinkTooltip::AddRuleButton_Click(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        const auto rule = _ViewModel.RequestAddRule();
        _ViewModel.CurrentRule(rule);
    }

    void LinkTooltip::DeleteRule_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        const auto rule = sender.as<Controls::Button>().DataContext().as<Editor::HyperlinkTooltipRuleViewModel>();
        _ViewModel.RequestDeleteRule(rule);
    }

    void LinkTooltip::ReorderRule_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        const auto btn = sender.as<Controls::Button>();
        const auto rule = btn.DataContext().as<Editor::HyperlinkTooltipRuleViewModel>();
        const auto direction = unbox_value<hstring>(btn.Tag());
        _ViewModel.RequestReorderRule(rule, direction == L"Up");
    }

    void LinkTooltip::EditRule_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        const auto rule = sender.as<Controls::Button>().DataContext().as<Editor::HyperlinkTooltipRuleViewModel>();
        _ViewModel.CurrentRule(rule);
    }

    void LinkTooltip::CloseRuleEditor_Click(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        _ViewModel.CurrentRule(nullptr);
    }

    void LinkTooltip::AddCustomActionButton_Click(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        if (const auto rule = _ViewModel.CurrentRule())
        {
            rule.RequestAddCustomAction();
        }
    }

    void LinkTooltip::DeleteCustomAction_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        if (const auto rule = _ViewModel.CurrentRule())
        {
            const auto action = sender.as<Controls::Button>().DataContext().as<Editor::HyperlinkTooltipActionViewModel>();
            rule.RequestDeleteCustomAction(action);
        }
    }

    // IconPicker's file-browse mode needs an HWND to parent its dialog. It's only reachable
    // through the page (see NewTabMenu's identical WindowRoot handoff), and each custom
    // action's picker lives inside a per-row DataTemplate rather than the page's own data
    // context, so it's wired up here on Loaded instead of through x:Bind.
    void LinkTooltip::CustomActionIconPicker_Loaded(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        if (const auto picker = sender.try_as<Editor::IconPicker>())
        {
            picker.WindowRoot(WindowRoot());
        }
    }
}
