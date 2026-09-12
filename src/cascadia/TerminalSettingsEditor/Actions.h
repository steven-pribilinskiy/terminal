// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "Actions.g.h"
#include "ActionsViewModel.h"
#include "Utils.h"
#include "ViewModelHelpers.h"

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    struct Actions : public HasScrollViewer<Actions>, ActionsT<Actions>
    {
    public:
        Actions();

        void OnNavigatedTo(const winrt::Windows::UI::Xaml::Navigation::NavigationEventArgs& e);
        void OnNavigatedFrom(const winrt::Windows::UI::Xaml::Navigation::NavigationEventArgs& e);

        til::property_changed_event PropertyChanged;
        WINRT_OBSERVABLE_PROPERTY(Editor::ActionsViewModel, ViewModel, PropertyChanged.raise, nullptr);

    private:
        winrt::Windows::UI::Xaml::FrameworkElement::LayoutUpdated_revoker _layoutUpdatedRevoker;
        Editor::ActionsViewModel::FocusKeyChordContainerRequested_revoker _focusKeyChordContainerRevoker;

        void _FocusKeyChordContainer(const Editor::CommandViewModel& cmdVM, const Editor::KeyChordViewModel& kcVM);

        // The shortcuts list needs a real MaxHeight to virtualize at all (see the
        // comment on CommandsListView in Actions.xaml), and it should be the room left
        // in the settings page's viewport rather than a guess. Both live here because
        // the ScrollViewer is the hosting page's, above this page in the tree.
        void _ArmCommandsListHeightTracking();
        void _UpdateCommandsListHeight();

        winrt::weak_ref<winrt::Windows::UI::Xaml::Controls::ScrollViewer> _pageScrollViewer;
        winrt::Windows::UI::Xaml::FrameworkElement::SizeChanged_revoker _scrollViewerSizeChangedRevoker;
    };
}

namespace winrt::Microsoft::Terminal::Settings::Editor::factory_implementation
{
    BASIC_FACTORY(Actions);
}
