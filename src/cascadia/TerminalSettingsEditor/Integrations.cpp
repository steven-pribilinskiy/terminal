// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "Integrations.h"
#include "Integrations.g.cpp"

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Navigation;
using namespace winrt::Microsoft::Terminal::Settings::Model;

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    Integrations::Integrations()
    {
        InitializeComponent();
    }

    void Integrations::OnNavigatedTo(const NavigationEventArgs& e)
    {
        const auto args = e.Parameter().as<Editor::NavigateToPageArgs>();
        _ViewModel = args.ViewModel().as<Editor::IntegrationsViewModel>();
        BringIntoViewWhenLoaded(args.ElementToFocus());

        TraceLoggingWrite(
            g_hTerminalSettingsEditorProvider,
            "NavigatedToPage",
            TraceLoggingDescription("Event emitted when the user navigates to a page in the settings UI"),
            TraceLoggingValue("integrations", "PageId", "The identifier of the page that was navigated to"),
            TraceLoggingKeyword(MICROSOFT_KEYWORD_MEASURES),
            TelemetryPrivacyDataTag(PDT_ProductAndServiceUsage));
    }

    void Integrations::IntegrationNavigator_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        const auto integration = sender.as<FrameworkElement>().Tag().as<Editor::IntegrationViewModel>();
        _ViewModel.CurrentIntegration(integration);
        RuleAddedInfoBar().IsOpen(false);
    }

    void Integrations::CloseIntegrationEditor_Click(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        _ViewModel.CurrentIntegration(nullptr);
        RuleAddedInfoBar().IsOpen(false);
    }

    // A PasswordBox's contents can't be read from the Save button's own data
    // context, so each box pushes what it holds into its view model as it's typed.
    // The value never leaves memory until Save writes it to the credential vault.
    void Integrations::CredentialPassword_Changed(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        const auto box = sender.as<Controls::PasswordBox>();
        if (const auto credential = box.Tag().try_as<Editor::IntegrationCredentialViewModel>())
        {
            credential.PendingValue(box.Password());
        }
    }

    void Integrations::SaveCredential_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        const auto credential = sender.as<FrameworkElement>().Tag().as<Editor::IntegrationCredentialViewModel>();
        // Save clears PendingValue, which empties the bound PasswordBox.
        credential.Save(credential.PendingValue());
    }

    void Integrations::ClearCredential_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        const auto credential = sender.as<FrameworkElement>().Tag().as<Editor::IntegrationCredentialViewModel>();
        credential.Clear();
    }

    void Integrations::AddMatcherAsRule_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        const auto matcher = sender.as<FrameworkElement>().Tag().as<Editor::IntegrationMatcherViewModel>();
        matcher.AddAsRule();

        // The rule lands on the Link Tooltip page; say so rather than yanking the
        // user over there mid-configuration. The bar's action button navigates.
        RuleAddedInfoBar().IsOpen(true);
    }

    void Integrations::GoToLinkTooltip_Click(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        RuleAddedInfoBar().IsOpen(false);
        _ViewModel.RequestNavigateToLinkTooltip();
    }
}
