// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "InteractionViewModel.h"
#include "InteractionViewModel.g.cpp"
#include "EnumEntry.h"

using namespace winrt::Windows::UI::Xaml::Navigation;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::Terminal::Settings::Model;

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    InteractionViewModel::InteractionViewModel(Model::GlobalAppSettings globalSettings, Model::WindowSettings windowSettings) :
        _GlobalSettings{ globalSettings },
        _WindowSettings{ windowSettings }
    {
        INITIALIZE_BINDABLE_ENUM_SETTING(TabSwitcherMode, TabSwitcherMode, TabSwitcherMode, L"Globals_TabSwitcherMode", L"Content");
        INITIALIZE_BINDABLE_ENUM_SETTING(CopyFormat, CopyFormat, winrt::Microsoft::Terminal::Control::CopyFormat, L"Globals_CopyFormat", L"Content");
        INITIALIZE_BINDABLE_ENUM_SETTING(ConfirmOnClose, ConfirmOnClose, Model::ConfirmOnClose, L"Globals_ConfirmOnClose", L"Content");
        INITIALIZE_BINDABLE_ENUM_SETTING(SettingsUIHost, SettingsUIHost, Model::SettingsUIHost, L"Globals_SettingsUIHost", L"Content");
    }

    // The threshold only means anything for the option that reads it.
    bool InteractionViewModel::ConfirmOnCloseThresholdEnabled()
    {
        return _GlobalSettings.ConfirmOnClose() == Model::ConfirmOnClose::MoreThanTabs;
    }

    // The threshold box's IsEnabled depends on the dropdown beside it, and
    // nothing else would tell it that the choice moved:
    // GETSET_BINDABLE_ENUM_SETTING's setter writes straight through to the
    // settings without raising PropertyChanged. Same reason as
    // GlobalAppearanceViewModel::TabPositionChanged.
    //
    // Notifying only the derived property, never the one the binding just wrote
    // - that way round is what sends the Settings UI into a loop.
    void InteractionViewModel::ConfirmOnCloseChanged(const winrt::Windows::Foundation::IInspectable& /* sender */, const winrt::Windows::UI::Xaml::Controls::SelectionChangedEventArgs& /* args */)
    {
        _NotifyChanges(L"ConfirmOnCloseThresholdEnabled");
    }

}
