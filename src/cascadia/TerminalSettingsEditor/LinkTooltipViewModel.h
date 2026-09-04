// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "LinkTooltipViewModel.g.h"
#include "HyperlinkTooltipRuleViewModel.g.h"
#include "HyperlinkTooltipActionViewModel.g.h"
#include "IntegrationChoiceViewModel.g.h"
#include "ButtonChoiceViewModel.g.h"
#include "ViewModelHelpers.h"
#include "Utils.h"

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    struct IntegrationChoiceViewModel : IntegrationChoiceViewModelT<IntegrationChoiceViewModel>
    {
    public:
        IntegrationChoiceViewModel(hstring id, hstring displayName) :
            _Id{ std::move(id) },
            _DisplayName{ std::move(displayName) } {}

        hstring Id() const noexcept { return _Id; }
        hstring DisplayName() const noexcept { return _DisplayName; }

    private:
        hstring _Id;
        hstring _DisplayName;
    };

    // A checkbox for one built-in card button. It owns no state of its own: the
    // two callbacks read and write whichever id list the choice belongs to, so
    // the checkboxes can never drift from the list that is actually saved.
    struct ButtonChoiceViewModel : ButtonChoiceViewModelT<ButtonChoiceViewModel>, ViewModelHelper<ButtonChoiceViewModel>
    {
    public:
        using SelectedGetter = std::function<bool(const hstring&)>;
        using SelectedSetter = std::function<void(const hstring&, bool)>;

        ButtonChoiceViewModel(hstring id, hstring label, SelectedGetter getter, SelectedSetter setter) :
            _Id{ std::move(id) },
            _Label{ std::move(label) },
            _getter{ std::move(getter) },
            _setter{ std::move(setter) } {}

        using ViewModelHelper<ButtonChoiceViewModel>::PropertyChanged;

        hstring Id() const noexcept { return _Id; }
        hstring Label() const noexcept { return _Label; }

        bool Selected() const { return _getter ? _getter(_Id) : false; }
        void Selected(bool value)
        {
            if (!_setter || Selected() == value)
            {
                return;
            }
            _setter(_Id, value);
            _NotifyChanges(L"Selected");
        }

        // Called by the owner when the whole list changed out from under the
        // checkboxes (the override toggle flipping, say). Not in the IDL --
        // only the owning view model, which holds the implementation type, calls it.
        void RaiseSelectedChanged() { _NotifyChanges(L"Selected"); }

    private:
        hstring _Id;
        hstring _Label;
        SelectedGetter _getter;
        SelectedSetter _setter;
    };

    struct HyperlinkTooltipActionViewModel : HyperlinkTooltipActionViewModelT<HyperlinkTooltipActionViewModel>, ViewModelHelper<HyperlinkTooltipActionViewModel>
    {
    public:
        HyperlinkTooltipActionViewModel(Model::HyperlinkTooltipAction action) :
            _Action{ action } {}

        // DON'T YOU DARE ADD A `WINRT_CALLBACK(PropertyChanged` TO A CLASS DERIVED FROM ViewModelHelper. Do this instead:
        using ViewModelHelper<HyperlinkTooltipActionViewModel>::PropertyChanged;

        GETSET_OBSERVABLE_PROJECTED_SETTING(_Action, Name);
        GETSET_OBSERVABLE_PROJECTED_SETTING(_Action, ActionId);

        hstring Icon() const { return _Action.Icon().Path(); }
        void Icon(const hstring& value)
        {
            _Action.Icon(Model::MediaResourceHelper::FromString(value));
            _NotifyChanges(L"Icon");
        }

        Model::HyperlinkTooltipAction Action() const noexcept { return _Action; }

    private:
        Model::HyperlinkTooltipAction _Action;
    };

    struct HyperlinkTooltipRuleViewModel : HyperlinkTooltipRuleViewModelT<HyperlinkTooltipRuleViewModel>, ViewModelHelper<HyperlinkTooltipRuleViewModel>
    {
    public:
        HyperlinkTooltipRuleViewModel(Model::HyperlinkTooltipRule rule, Model::WindowSettings windowSettings);

        using ViewModelHelper<HyperlinkTooltipRuleViewModel>::PropertyChanged;

        GETSET_OBSERVABLE_PROJECTED_SETTING(_Rule, Name);
        GETSET_OBSERVABLE_PROJECTED_SETTING(_Rule, Enabled);
        GETSET_OBSERVABLE_PROJECTED_SETTING(_Rule, Integration);
        GETSET_OBSERVABLE_PROJECTED_SETTING(_Rule, ShowPreview);

    public:
        // Not the projected-setting macro, because the pattern is half of what
        // the rules list shows as a rule's summary, and now that the list is a
        // view of its own an edit here has to reach it.
        hstring Pattern() const { return _Rule.Pattern(); }
        void Pattern(const hstring& value)
        {
            if (_Rule.Pattern() != value)
            {
                _Rule.Pattern(value);
                _NotifyChanges(L"Pattern", L"SummaryText");
            }
        }

        // Same reason, and the macro's own setter doesn't notify at all: hence
        // the accessor pairs below rather than _Rule.FileTypeGroup / _Rule.Kind.
        GETSET_BINDABLE_ENUM_SETTING(FileTypeGroup, Model::HyperlinkFileTypeGroup, _fileTypeGroupAccessor);

        // Kind additionally decides which cards the editor even shows.
        GETSET_BINDABLE_ENUM_SETTING(Kind, Model::HyperlinkMatchKind, _kindAccessor);

    public:
        bool IsLinkKind() const noexcept { return _Rule.Kind() == Model::HyperlinkMatchKind::Link; }
        bool IsTextKind() const noexcept { return _Rule.Kind() == Model::HyperlinkMatchKind::Text; }

        Windows::Foundation::Collections::IVector<Editor::IntegrationChoiceViewModel> IntegrationChoices() const noexcept { return _IntegrationChoices; }
        Windows::Foundation::IInspectable CurrentIntegrationChoice() const;
        void CurrentIntegrationChoice(const Windows::Foundation::IInspectable& value);

        hstring SummaryText() const;

        hstring Schemes() const;
        void Schemes(const hstring& value);
        hstring CustomExtensions() const;
        void CustomExtensions(const hstring& value);

        bool OverrideShowDelay() const noexcept { return static_cast<bool>(_Rule.TooltipShowDelay()); }
        void OverrideShowDelay(bool value);
        int32_t ShowDelay() const noexcept;
        void ShowDelay(int32_t value);

        bool OverrideHideDelay() const noexcept { return static_cast<bool>(_Rule.TooltipHideDelay()); }
        void OverrideHideDelay(bool value);
        int32_t HideDelay() const noexcept;
        void HideDelay(int32_t value);

        bool OverrideMaxWidth() const noexcept { return static_cast<bool>(_Rule.TooltipMaxWidth()); }
        void OverrideMaxWidth(bool value);
        int32_t MaxWidth() const noexcept;
        void MaxWidth(int32_t value);

        // A rule with no buttons of its own inherits the global list, so an
        // empty (or absent) list is exactly what "don't override" means.
        bool OverrideButtons() const noexcept;
        void OverrideButtons(bool value);
        Windows::Foundation::Collections::IObservableVector<Editor::ButtonChoiceViewModel> ButtonChoices() const noexcept { return _ButtonChoices; }

        bool OverrideShowInPane() const noexcept { return static_cast<bool>(_Rule.ShowInPane()); }
        void OverrideShowInPane(bool value);
        bool ShowInPane() const noexcept;
        void ShowInPane(bool value);

        Windows::Foundation::Collections::IObservableVector<Editor::HyperlinkTooltipActionViewModel> CustomActions() const noexcept { return _CustomActions; }
        Editor::HyperlinkTooltipActionViewModel RequestAddCustomAction();
        void RequestDeleteCustomAction(const Editor::HyperlinkTooltipActionViewModel& vm);

        Model::HyperlinkTooltipRule Rule() const noexcept { return _Rule; }

    private:
        Model::HyperlinkTooltipRule _Rule;
        Model::WindowSettings _WindowSettings;
        Windows::Foundation::Collections::IObservableVector<Editor::HyperlinkTooltipActionViewModel> _CustomActions;
        Windows::Foundation::Collections::IObservableVector<Editor::HyperlinkTooltipActionViewModel>::VectorChanged_revoker _customActionsChangedRevoker;
        Windows::Foundation::Collections::IVector<Editor::IntegrationChoiceViewModel> _IntegrationChoices;
        Windows::Foundation::Collections::IObservableVector<Editor::ButtonChoiceViewModel> _ButtonChoices;

        void _raiseButtonChoicesChanged();

        Model::HyperlinkFileTypeGroup _fileTypeGroupAccessor() const { return _Rule.FileTypeGroup(); }
        void _fileTypeGroupAccessor(Model::HyperlinkFileTypeGroup value)
        {
            if (_Rule.FileTypeGroup() != value)
            {
                _Rule.FileTypeGroup(value);
                _NotifyChanges(L"SummaryText");
            }
        }

        Model::HyperlinkMatchKind _kindAccessor() const { return _Rule.Kind(); }
        void _kindAccessor(Model::HyperlinkMatchKind value)
        {
            if (_Rule.Kind() != value)
            {
                _Rule.Kind(value);
                _NotifyChanges(L"IsLinkKind", L"IsTextKind", L"SummaryText");
            }
        }
    };

    struct LinkTooltipViewModel : LinkTooltipViewModelT<LinkTooltipViewModel>, ViewModelHelper<LinkTooltipViewModel>
    {
    public:
        LinkTooltipViewModel(Model::GlobalAppSettings globalSettings, Model::WindowSettings windowSettings);

        using ViewModelHelper<LinkTooltipViewModel>::PropertyChanged;

        PERMANENT_OBSERVABLE_PROJECTED_SETTING(_WindowSettings, DetectURLs);
        PERMANENT_OBSERVABLE_PROJECTED_SETTING(_WindowSettings, OpenLinksOnSingleClick);
        PERMANENT_OBSERVABLE_PROJECTED_SETTING(_WindowSettings, HyperlinkTooltipMaxWidth);
        PERMANENT_OBSERVABLE_PROJECTED_SETTING(_WindowSettings, HyperlinkTooltipShowDelay);
        PERMANENT_OBSERVABLE_PROJECTED_SETTING(_WindowSettings, HyperlinkTooltipHideDelay);
        PERMANENT_OBSERVABLE_PROJECTED_SETTING(_WindowSettings, HyperlinkTooltipActions);
        PERMANENT_OBSERVABLE_PROJECTED_SETTING(_WindowSettings, HyperlinkTooltipHint);
        PERMANENT_OBSERVABLE_PROJECTED_SETTING(_WindowSettings, HyperlinkPreviewInPane);

        winrt::hstring SafeUriSchemes() const;
        void SafeUriSchemes(const winrt::hstring& value);

        Windows::Foundation::Collections::IObservableVector<Editor::ButtonChoiceViewModel> ButtonChoices() const noexcept { return _ButtonChoices; }

        Windows::Foundation::Collections::IObservableVector<Editor::HyperlinkTooltipRuleViewModel> CurrentView() const noexcept { return _CurrentView; }

        Editor::HyperlinkTooltipRuleViewModel CurrentRule() const noexcept { return _CurrentRule; }
        void CurrentRule(const Editor::HyperlinkTooltipRuleViewModel& vm);
        bool IsEditingRule() const noexcept { return static_cast<bool>(_CurrentRule); }
        bool IsNotEditingRule() const noexcept { return !_CurrentRule; }
        hstring CurrentRuleName() const { return _CurrentRule ? _CurrentRule.Name() : hstring{}; }

        void RequestReorderRule(const Editor::HyperlinkTooltipRuleViewModel& vm, bool goingUp);
        void RequestDeleteRule(const Editor::HyperlinkTooltipRuleViewModel& vm);
        Editor::HyperlinkTooltipRuleViewModel RequestAddRule();

    private:
        Model::GlobalAppSettings _GlobalSettings;
        Model::WindowSettings _WindowSettings;
        Windows::Foundation::Collections::IObservableVector<Editor::HyperlinkTooltipRuleViewModel> _CurrentView;
        Windows::Foundation::Collections::IObservableVector<Editor::HyperlinkTooltipRuleViewModel>::VectorChanged_revoker _rulesChangedRevoker;
        Windows::Foundation::Collections::IObservableVector<Editor::ButtonChoiceViewModel> _ButtonChoices;
        Editor::HyperlinkTooltipRuleViewModel _CurrentRule{ nullptr };
        // Watches the rule being edited so a rename reaches the breadcrumb.
        Windows::UI::Xaml::Data::INotifyPropertyChanged::PropertyChanged_revoker _currentRuleChangedRevoker;
    };
}

namespace winrt::Microsoft::Terminal::Settings::Editor::factory_implementation
{
    BASIC_FACTORY(HyperlinkTooltipActionViewModel);
    BASIC_FACTORY(HyperlinkTooltipRuleViewModel);
    BASIC_FACTORY(LinkTooltipViewModel);
    // ButtonChoiceViewModel and IntegrationChoiceViewModel declare no
    // constructor in the IDL -- their owner builds them -- so neither has an
    // activation factory.
}
