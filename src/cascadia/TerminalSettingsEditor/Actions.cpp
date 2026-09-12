// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "Actions.h"
#include "Actions.g.cpp"
#include "LibraryResources.h"
#include "../TerminalSettingsModel/AllShortcutActions.h"

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Navigation;

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    // Never let the list shrink below this, however short the window is. A list bound to
    // a handful of pixels is worse than one that overflows: the page scrolls, and a few
    // rows at a time is still usable.
    static constexpr double MinCommandsListHeight{ 180.0 };

    // How far MaxHeight has to move before it is worth writing. Setting it causes a
    // layout pass, and a layout pass is what gets us called again, so without a
    // deadband the two can chase each other by a fraction of a pixel forever.
    static constexpr double CommandsListHeightEpsilon{ 1.0 };

    // Walks up from the given element to the first ScrollViewer above it. Starting at
    // the page, not inside the list, so this finds the settings page's shared
    // ScrollViewer rather than the one inside the ListView's own template.
    static Controls::ScrollViewer _findAncestorScrollViewer(const Windows::UI::Xaml::DependencyObject& from)
    {
        for (auto current = from; current; current = Windows::UI::Xaml::Media::VisualTreeHelper::GetParent(current))
        {
            if (const auto scrollViewer = current.try_as<Controls::ScrollViewer>())
            {
                return scrollViewer;
            }
        }
        return nullptr;
    }

    // The bottom padding of the Frame the pages are hosted in, which is content inside
    // the ScrollViewer and below this page - so it has to come off the room available to
    // the list or the page acquires a scrollbar just to show it. Read from the Frame
    // rather than hardcoded, because the value lives in MainPage.xaml.
    static double _hostFrameBottomPadding(const Windows::UI::Xaml::DependencyObject& from)
    {
        for (auto current = from; current; current = Windows::UI::Xaml::Media::VisualTreeHelper::GetParent(current))
        {
            if (const auto frame = current.try_as<Controls::Frame>())
            {
                return frame.Padding().Bottom;
            }
        }
        return 0.0;
    }

    // Depth-first search of the visual tree under 'root' for the first ItemsControl. Used to
    // locate a command row's nested key chord list so we can resolve a specific chord's row.
    static Controls::ItemsControl _findChildItemsControl(const Windows::UI::Xaml::DependencyObject& root)
    {
        if (!root)
        {
            return nullptr;
        }
        const auto count = Windows::UI::Xaml::Media::VisualTreeHelper::GetChildrenCount(root);
        for (int32_t i = 0; i < count; ++i)
        {
            const auto child = Windows::UI::Xaml::Media::VisualTreeHelper::GetChild(root, i);
            if (const auto itemsControl = child.try_as<Controls::ItemsControl>())
            {
                return itemsControl;
            }
            if (const auto found = _findChildItemsControl(child))
            {
                return found;
            }
        }
        return nullptr;
    }

    Actions::Actions()
    {
        InitializeComponent();

        Automation::AutomationProperties::SetName(AddNewButton(), RS_(L"Actions_AddNewTextBlock/Text"));
    }

    // Called when a key chord row enters or leaves inline edit mode. When entering, focus that
    // row's KeyChordListener so the user can immediately type a chord. When leaving, return
    // focus to the row's first focusable control (the edit pencil).
    void Actions::_FocusKeyChordContainer(const Editor::CommandViewModel& cmdVM, const Editor::KeyChordViewModel& kcVM)
    {
        if (!cmdVM || !kcVM)
        {
            return;
        }

        // Defer to the next dispatcher tick: when entering edit mode the listener's
        // Visibility="{x:Bind IsInEditMode}" binding (and the corresponding layout) has not
        // applied yet, so focusing it synchronously would be a no-op (can't focus a collapsed
        // element). Running after the tick lets visibility/layout settle first.
        auto weakThis{ get_weak() };
        winrt::Windows::System::DispatcherQueue::GetForCurrentThread().TryEnqueue([weakThis, cmdVM, kcVM]() {
            const auto self{ weakThis.get() };
            if (!self)
            {
                return;
            }

            const auto cmdContainer = self->CommandsListView().ContainerFromItem(cmdVM);
            if (!cmdContainer)
            {
                return;
            }
            // Realize the containers in case this row was just added/revealed.
            self->CommandsListView().UpdateLayout();

            const auto cmdRoot = cmdContainer.try_as<DependencyObject>();
            const auto kcItemsControl = _findChildItemsControl(cmdRoot);
            if (!kcItemsControl)
            {
                return;
            }
            kcItemsControl.UpdateLayout();

            const auto rowContainer = kcItemsControl.ContainerFromItem(kcVM);
            if (!rowContainer)
            {
                return;
            }
            const auto rowRoot = rowContainer.try_as<DependencyObject>();

            if (kcVM.IsInEditMode())
            {
                // Focus the editable listener so the user can type a chord.
                if (const auto listener = FindKeyChordListener(rowRoot))
                {
                    listener.FocusInput();
                    return;
                }
            }

            // Otherwise (left edit mode) return focus to the row's first focusable control.
            if (const auto focusable = FindFirstFocusable(rowRoot))
            {
                focusable.Focus(FocusState::Programmatic);
            }
        });
    }

    // Finds the hosting ScrollViewer and starts following its size. Called from the
    // one-shot LayoutUpdated in OnNavigatedTo rather than from Loaded, because what this
    // needs is a ViewportHeight, and that is only meaningful once a layout pass has
    // actually run.
    void Actions::_ArmCommandsListHeightTracking()
    {
        const auto scrollViewer = _findAncestorScrollViewer(*this);
        if (!scrollViewer)
        {
            // No ScrollViewer above us: leave the MaxHeight from the markup alone. It is
            // a guess, but it is a bounded guess, which is the part virtualization needs.
            return;
        }

        _pageScrollViewer = scrollViewer;
        _scrollViewerSizeChangedRevoker = scrollViewer.SizeChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
            _UpdateCommandsListHeight();
        });

        _UpdateCommandsListHeight();
    }

    // Bounds the list to the room left below it in the viewport, so it fills the window
    // instead of stopping at whatever number the markup happened to carry.
    //
    // Derived from the ScrollViewer's ViewportHeight, never from this page's
    // ActualHeight: the page is measured with infinite height, so its ActualHeight is
    // the height it would like to be, which is a function of the list's height and
    // therefore says nothing about the window.
    void Actions::_UpdateCommandsListHeight()
    {
        const auto scrollViewer = _pageScrollViewer.get();
        const auto list = CommandsListView();
        if (!scrollViewer || !list)
        {
            return;
        }

        const auto viewport = scrollViewer.ViewportHeight();
        if (!(viewport > 0))
        {
            // Not measured yet, or measured to nothing. Either way there is no answer to
            // give, and writing one would be a guess dressed up as a measurement.
            return;
        }

        // Where the list starts within the scrollable content. TransformToVisual is
        // relative to the ScrollViewer's viewport, so it moves as the page scrolls;
        // adding the current offset back converts it to a position in the content, which
        // is what stays put.
        double listTop = 0;
        try
        {
            const auto transform = list.TransformToVisual(scrollViewer);
            listTop = transform.TransformPoint({ 0.0f, 0.0f }).Y + scrollViewer.VerticalOffset();
        }
        catch (...)
        {
            // TransformToVisual throws if the two are not in the same tree, which happens
            // while the page is being torn down.
            LOG_CAUGHT_EXCEPTION();
            return;
        }

        const auto available = viewport - listTop - _hostFrameBottomPadding(*this);
        const auto target = std::max(MinCommandsListHeight, available);

        if (std::abs(list.MaxHeight() - target) > CommandsListHeightEpsilon)
        {
            list.MaxHeight(target);
        }
    }

    void Actions::OnNavigatedTo(const NavigationEventArgs& e)
    {
        const auto args = e.Parameter().as<Editor::NavigateToPageArgs>();
        _ViewModel = args.ViewModel().as<Editor::ActionsViewModel>();
        _ViewModel.ReSortCommandList();
        _focusKeyChordContainerRevoker = _ViewModel.FocusKeyChordContainerRequested(winrt::auto_revoke, { this, &Actions::_FocusKeyChordContainer });
        auto vmImpl = get_self<ActionsViewModel>(_ViewModel);
        vmImpl->MarkAsVisited();
        _layoutUpdatedRevoker = LayoutUpdated(winrt::auto_revoke, [this](auto /*s*/, auto /*e*/) {
            // Only let this succeed once.
            _layoutUpdatedRevoker.revoke();

            AddNewButton().Focus(FocusState::Programmatic);

            // Here rather than in the constructor or in Loaded: a layout pass has now
            // run, so the hosting ScrollViewer has a ViewportHeight to read and this page
            // has a position within it.
            _ArmCommandsListHeightTracking();
        });
        BringIntoViewWhenLoaded(args.ElementToFocus());

        TraceLoggingWrite(
            g_hTerminalSettingsEditorProvider,
            "NavigatedToPage",
            TraceLoggingDescription("Event emitted when the user navigates to a page in the settings UI"),
            TraceLoggingValue("actions", "PageId", "The identifier of the page that was navigated to"),
            TraceLoggingKeyword(MICROSOFT_KEYWORD_MEASURES),
            TelemetryPrivacyDataTag(PDT_ProductAndServiceUsage));
    }

    void Actions::OnNavigatedFrom(const NavigationEventArgs& /*e*/)
    {
        _focusKeyChordContainerRevoker.revoke();
        _layoutUpdatedRevoker.revoke();
        _scrollViewerSizeChangedRevoker.revoke();
        _pageScrollViewer = {};

        if (_ViewModel)
        {
            get_self<ActionsViewModel>(_ViewModel)->CancelPendingKeyChordEdit();
        }
    }
}
