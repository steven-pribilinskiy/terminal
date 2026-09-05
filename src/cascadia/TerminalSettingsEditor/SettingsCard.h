/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- SettingsCard

Abstract:
- A base control for building consistent settings experiences. Based
  on the Windows Community Toolkit's SettingsCard.

Author(s):
- Carlos Zamora - 2026 May

--*/

#pragma once

#include "SettingsCard.g.h"
#include "Utils.h"

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    struct SettingsCard : SettingsCardT<SettingsCard>
    {
    public:
        SettingsCard();

        void OnApplyTemplate();
        void OnPointerPressed(const Windows::UI::Xaml::Input::PointerRoutedEventArgs& e);
        void OnPointerReleased(const Windows::UI::Xaml::Input::PointerRoutedEventArgs& e);

        // Automation peer override.
        Windows::UI::Xaml::Automation::Peers::AutomationPeer OnCreateAutomationPeer();

        DEPENDENCY_PROPERTY(Windows::Foundation::IInspectable, Header);
        DEPENDENCY_PROPERTY(Windows::Foundation::IInspectable, Description);
        DEPENDENCY_PROPERTY(Windows::UI::Xaml::Controls::IconElement, HeaderIcon);
        DEPENDENCY_PROPERTY(Windows::Foundation::IInspectable, ActionIcon);
        DEPENDENCY_PROPERTY(hstring, ActionIconToolTip);
        DEPENDENCY_PROPERTY(bool, IsClickEnabled);
        DEPENDENCY_PROPERTY(bool, IsActionIconVisible);
        DEPENDENCY_PROPERTY(Editor::SettingsCardContentAlignment, ContentAlignment);
        DEPENDENCY_PROPERTY(bool, IsForkFeature);

        // Whether fork-only rows draw their mark, for the whole settings window.
        //
        // App-wide state rather than another dependency property, because the
        // alternative is threading one binding through every page's view model to
        // reach cards that otherwise have nothing to do with it. Setting it walks
        // the cards already on screen so the switch takes effect where you flipped
        // it, not on the next navigation.
        static bool ImprintEnabled() noexcept;
        static void ImprintEnabled(bool value);

    private:
        static void _InitializeProperties();
        static void _OnHeaderChanged(const Windows::UI::Xaml::DependencyObject& d, const Windows::UI::Xaml::DependencyPropertyChangedEventArgs& e);
        static void _OnDescriptionChanged(const Windows::UI::Xaml::DependencyObject& d, const Windows::UI::Xaml::DependencyPropertyChangedEventArgs& e);
        static void _OnHeaderIconChanged(const Windows::UI::Xaml::DependencyObject& d, const Windows::UI::Xaml::DependencyPropertyChangedEventArgs& e);
        static void _OnIsClickEnabledChanged(const Windows::UI::Xaml::DependencyObject& d, const Windows::UI::Xaml::DependencyPropertyChangedEventArgs& e);
        static void _OnIsActionIconVisibleChanged(const Windows::UI::Xaml::DependencyObject& d, const Windows::UI::Xaml::DependencyPropertyChangedEventArgs& e);
        static void _OnContentAlignmentChanged(const Windows::UI::Xaml::DependencyObject& d, const Windows::UI::Xaml::DependencyPropertyChangedEventArgs& e);
        static void _OnIsForkFeatureChanged(const Windows::UI::Xaml::DependencyObject& d, const Windows::UI::Xaml::DependencyPropertyChangedEventArgs& e);

        void _UpdateForkImprint();

        // Every card built so far, weakly. Weak because XAML owns their lifetime and
        // a settings window opens and closes many times in a session; dead entries
        // are dropped on the next walk rather than needing an unregister step.
        static inline bool _imprintEnabled{ false };
        static inline std::vector<winrt::weak_ref<Editor::SettingsCard>> _liveCards;

        void _EnableButtonInteraction();
        void _DisableButtonInteraction();
        void _GoToCommonState(const std::wstring_view& state, bool useTransitions);
        void _UpdateActionIconVisibility();
        void _EnsureDefaultActionIcon();
        void _UpdateHeaderVisibility();
        void _UpdateDescriptionVisibility();
        void _UpdateFullDescription();
        void _UpdateHeaderIconVisibility();
        void _UpdateContentVisibility();
        void _UpdateContentAlignmentState();
        void _CheckInitialVisualState();
        void _CheckHeaderIconState();
        void _CheckVerticalSpacingState(const Windows::UI::Xaml::VisualState& state);
        void _SetAccessibleContentName();
        Windows::UI::Xaml::FrameworkElement _GetFocusedElement();

        bool _interactionEnabled{ false };
        Windows::UI::Xaml::Controls::Control::IsEnabledChanged_revoker _isEnabledChangedRevoker;
        Windows::UI::Xaml::UIElement::PointerEntered_revoker _pointerEnteredRevoker;
        Windows::UI::Xaml::UIElement::PointerExited_revoker _pointerExitedRevoker;
        Windows::UI::Xaml::UIElement::PointerCaptureLost_revoker _pointerCaptureLostRevoker;
        Windows::UI::Xaml::UIElement::PointerCanceled_revoker _pointerCanceledRevoker;
        Windows::UI::Xaml::UIElement::PreviewKeyDown_revoker _previewKeyDownRevoker;
        Windows::UI::Xaml::UIElement::PreviewKeyUp_revoker _previewKeyUpRevoker;
        Windows::UI::Xaml::VisualStateGroup::CurrentStateChanged_revoker _contentAlignmentStatesChangedRevoker;
        int64_t _contentChangedToken{ 0 };
    };

    // AutomationPeer for SettingsCard. Mirrors the Community Toolkit's
    // SettingsCardAutomationPeer: only exposes Invoke + Button control type when
    // the card has IsClickEnabled=true; otherwise reports as a Group.
    struct SettingsCardAutomationPeer : Windows::UI::Xaml::Automation::Peers::ButtonBaseAutomationPeerT<SettingsCardAutomationPeer>
    {
    public:
        SettingsCardAutomationPeer(const Editor::SettingsCard& owner);

        Windows::UI::Xaml::Automation::Peers::AutomationControlType GetAutomationControlTypeCore() const;
        hstring GetClassNameCore() const;
        hstring GetNameCore() const;
        winrt::Windows::Foundation::IInspectable GetPatternCore(Windows::UI::Xaml::Automation::Peers::PatternInterface patternInterface) const;
    };
}

namespace winrt::Microsoft::Terminal::Settings::Editor::factory_implementation
{
    BASIC_FACTORY(SettingsCard);
}
