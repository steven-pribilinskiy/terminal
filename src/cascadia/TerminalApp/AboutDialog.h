// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "AboutDialog.g.h"

namespace winrt::TerminalApp::implementation
{
    struct AboutDialog : AboutDialogT<AboutDialog>
    {
    public:
        AboutDialog();

        winrt::hstring ApplicationDisplayName();
        winrt::hstring ApplicationVersion();
        winrt::hstring BuildCommit();
        winrt::hstring BuildBranch();
        winrt::hstring BuildTime();
        winrt::hstring BuildExePath();
        winrt::hstring BuildInfoForClipboard();

        til::property_changed_event PropertyChanged;
        WINRT_OBSERVABLE_PROPERTY(bool, UpdatesAvailable, PropertyChanged.raise, false);
        WINRT_OBSERVABLE_PROPERTY(bool, CheckingForUpdates, PropertyChanged.raise, false);
        // What kind of update was found, and which source reported it. "An update
        // is available" on its own says neither, which is actively confusing in dev
        // builds, where the answer is "none, this is a hardcoded stub".
        WINRT_OBSERVABLE_PROPERTY(winrt::hstring, UpdateStatusText, PropertyChanged.raise);

    private:
        friend struct AboutDialogT<AboutDialog>; // for Xaml to bind events

        std::chrono::system_clock::time_point _lastUpdateCheck{};

        void _ThirdPartyNoticesOnClick(const IInspectable& sender, const Windows::UI::Xaml::RoutedEventArgs& eventArgs);
        void _CopyBuildInfoOnClick(const IInspectable& sender, const Windows::UI::Xaml::RoutedEventArgs& eventArgs);
        void _SendFeedbackOnClick(const IInspectable& sender, const Windows::UI::Xaml::Controls::ContentDialogButtonClickEventArgs& eventArgs);
        safe_void_coroutine _queueUpdateCheck();
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(AboutDialog);
}
