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
    // Returns the first PasswordBox in a subtree, or null.
    static Controls::PasswordBox _findPasswordBox(const DependencyObject& root)
    {
        if (!root)
        {
            return nullptr;
        }
        if (const auto box = root.try_as<Controls::PasswordBox>())
        {
            return box;
        }

        const auto count = Media::VisualTreeHelper::GetChildrenCount(root);
        for (auto i = 0; i < count; ++i)
        {
            if (const auto box = _findPasswordBox(Media::VisualTreeHelper::GetChild(root, i)))
            {
                return box;
            }
        }
        return nullptr;
    }

    // Walks up from a credential row's button to the panel that also holds its
    // PasswordBox. The rows come from a DataTemplate, so the box has no name in
    // the page's namescope and has to be found relative to the clicked button.
    // The walk stops well short of the ItemsControl, so it can never reach into
    // a different credential's row.
    static Controls::PasswordBox _credentialBoxFor(const IInspectable& sender)
    {
        auto current = sender.try_as<DependencyObject>();
        for (auto depth = 0; current && depth < 3; ++depth)
        {
            current = Media::VisualTreeHelper::GetParent(current);
            if (const auto box = _findPasswordBox(current))
            {
                return box;
            }
        }
        return nullptr;
    }

    // The box is deliberately not bound to PendingValue -- Password plus
    // PasswordChanged on one control is re-entrant -- so emptying it after a save
    // or a clear happens here. Nothing is lost: a secret in the vault can never be
    // read back to re-fill the box anyway.
    static void _clearCredentialBox(const IInspectable& sender)
    {
        if (const auto box = _credentialBoxFor(sender))
        {
            box.Password(hstring{});
        }
    }

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
        // PendingValue is what the box last pushed in; read it before the box is emptied.
        credential.Save(credential.PendingValue());
        _clearCredentialBox(sender);
    }

    void Integrations::ClearCredential_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        const auto credential = sender.as<FrameworkElement>().Tag().as<Editor::IntegrationCredentialViewModel>();
        credential.Clear();
        _clearCredentialBox(sender);
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
