// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "winrt/Microsoft.UI.Xaml.Controls.h"

#include "TabRowControl.g.h"

namespace winrt::TerminalApp::implementation
{
    struct TabRowControl : TabRowControlT<TabRowControl>
    {
        TabRowControl();

        void OnNewTabButtonClick(const Windows::Foundation::IInspectable& sender, const Microsoft::UI::Xaml::Controls::SplitButtonClickEventArgs& args);
        void OnNewTabButtonDrop(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::DragEventArgs& e);
        void OnNewTabButtonDragOver(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::DragEventArgs& e);
        void OnPromoteSlotButtonClick(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& args);
        void OnSlotBadgeClick(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::UI::Xaml::RoutedEventArgs& args);

        til::typed_event<> PromoteRequested;
        // The badge reports the intent; TerminalPage owns the clipboard and the
        // toast, the same split PromoteRequested uses.
        til::typed_event<> CopyBuildInfoRequested;

        til::property_changed_event PropertyChanged;
        WINRT_OBSERVABLE_PROPERTY(bool, ShowElevationShield, PropertyChanged.raise, false);
        WINRT_OBSERVABLE_PROPERTY(bool, ShowWorkspacesButton, PropertyChanged.raise, true);
        WINRT_OBSERVABLE_PROPERTY(winrt::hstring, WorkspaceName, PropertyChanged.raise, L"");
        // A newer build is staged for the Dev slot and can be promoted into it.
        // Only ever true in the Dev slot; see SlotPromotion.h.
        WINRT_OBSERVABLE_PROPERTY(bool, PromotionAvailable, PropertyChanged.raise, false);
        // What is staged, for the button's tooltip: "a1b2c3d+dirty (main), 4 minutes ago".
        WINRT_OBSERVABLE_PROPERTY(winrt::hstring, PromotionDescription, PropertyChanged.raise, L"");

        // WINRT_OBSERVABLE_PROPERTY leaves the class in a non-public section, so
        // anything declared after it has to reopen public access explicitly.
    public:
        // Which local slot this window is: DEV, TEST, or empty for a normal
        // install (in which case the badge is hidden entirely).
        winrt::hstring SlotBadge();
        bool ShowSlotBadge();
        winrt::hstring SlotBadgeTooltipTitle();
        winrt::hstring BuildCommit();
        winrt::hstring BuildBranch();
        winrt::hstring BuildTime();
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(TabRowControl);
}
