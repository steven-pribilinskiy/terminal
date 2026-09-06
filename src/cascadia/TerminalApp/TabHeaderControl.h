// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "winrt/Microsoft.UI.Xaml.Controls.h"

#include "TabHeaderControl.g.h"

namespace winrt::TerminalApp::implementation
{
    struct TabHeaderControl : TabHeaderControlT<TabHeaderControl>
    {
        TabHeaderControl();
        void BeginRename();

        void RenameBoxLostFocusHandler(const winrt::Windows::Foundation::IInspectable& sender,
                                       const winrt::Windows::UI::Xaml::RoutedEventArgs& e);

        bool InRename();

        til::event<TerminalApp::TitleChangeRequestedArgs> TitleChangeRequested;
        til::typed_event<> RenameEnded;

        til::property_changed_event PropertyChanged;
        WINRT_OBSERVABLE_PROPERTY(winrt::hstring, Title, PropertyChanged.raise);
        // The tab's 1-based position, shown ahead of the title when
        // showTabIndex is on. Kept as a preformatted string so the control does
        // no arithmetic and the empty case costs nothing to render.
        WINRT_OBSERVABLE_PROPERTY(winrt::hstring, IndexLabel, PropertyChanged.raise);
        WINRT_OBSERVABLE_PROPERTY(bool, ShowIndex, PropertyChanged.raise, false);
        WINRT_OBSERVABLE_PROPERTY(double, RenamerMaxWidth, PropertyChanged.raise);
        WINRT_OBSERVABLE_PROPERTY(winrt::TerminalApp::TerminalTabStatus, TabStatus, PropertyChanged.raise);

    private:
        bool _receivedKeyDown{ false };
        bool _renameCancelled{ false };

        void _CloseRenameBox();
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(TabHeaderControl);
}
