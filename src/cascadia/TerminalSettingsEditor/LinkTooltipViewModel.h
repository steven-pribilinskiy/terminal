// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "LinkTooltipViewModel.g.h"
#include "HyperlinkTooltipRuleViewModel.g.h"
#include "HyperlinkTooltipActionViewModel.g.h"
#include "IntegrationChoiceViewModel.g.h"
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
        HyperlinkTooltipRuleViewModel(Model::HyperlinkTooltipRule rule);

        using ViewModelHelper<HyperlinkTooltipRuleViewModel>::PropertyChanged;

        GETSET_OBSERVABLE_PROJECTED_SETTING(_Rule, Name);
        GETSET_OBSERVABLE_PROJECTED_SETTING(_Rule, Enabled);
        GETSET_OBSERVABLE_PROJECTED_SETTING(_Rule, Pattern);
        GETSET_OBSERVABLE_PROJECTED_SETTING(_Rule, SuppressOpen);
        GETSET_OBSERVABLE_PROJECTED_SETTING(_Rule, SuppressCopyLink);
        GETSET_OBSERVABLE_PROJECTED_SETTING(_Rule, SuppressCopyPath);
        GETSET_OBSERVABLE_PROJECTED_SETTING(_Rule, SuppressReveal);
        GETSET_OBSERVABLE_PROJECTED_SETTING(_Rule, Integration);
        GETSET_OBSERVABLE_PROJECTED_SETTING(_Rule, ShowPreview);

        GETSET_BINDABLE_ENUM_SETTING(FileTypeGroup, Model::HyperlinkFileTypeGroup, _Rule.FileTypeGroup);

        // Unlike FileTypeGroup, Kind decides which cards the editor even shows, so
        // its setter has to raise IsLinkKind/IsTextKind. The macro's own setter
        // doesn't notify, hence the accessor pair below instead of _Rule.Kind.
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

        Windows::Foundation::Collections::IObservableVector<Editor::HyperlinkTooltipActionViewModel> CustomActions() const noexcept { return _CustomActions; }
        Editor::HyperlinkTooltipActionViewModel RequestAddCustomAction();
        void RequestDeleteCustomAction(const Editor::HyperlinkTooltipActionViewModel& vm);

        Model::HyperlinkTooltipRule Rule() const noexcept { return _Rule; }

    private:
        Model::HyperlinkTooltipRule _Rule;
        Windows::Foundation::Collections::IObservableVector<Editor::HyperlinkTooltipActionViewModel> _CustomActions;
        Windows::Foundation::Collections::IObservableVector<Editor::HyperlinkTooltipActionViewModel>::VectorChanged_revoker _customActionsChangedRevoker;
        Windows::Foundation::Collections::IVector<Editor::IntegrationChoiceViewModel> _IntegrationChoices;

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

        winrt::hstring SafeUriSchemes() const;
        void SafeUriSchemes(const winrt::hstring& value);

        Windows::Foundation::Collections::IObservableVector<Editor::HyperlinkTooltipRuleViewModel> CurrentView() const noexcept { return _CurrentView; }

        Editor::HyperlinkTooltipRuleViewModel CurrentRule() const noexcept { return _CurrentRule; }
        void CurrentRule(const Editor::HyperlinkTooltipRuleViewModel& vm)
        {
            _CurrentRule = vm;
            _NotifyChanges(L"CurrentRule", L"IsEditingRule");
        }
        bool IsEditingRule() const noexcept { return static_cast<bool>(_CurrentRule); }

        void RequestReorderRule(const Editor::HyperlinkTooltipRuleViewModel& vm, bool goingUp);
        void RequestDeleteRule(const Editor::HyperlinkTooltipRuleViewModel& vm);
        Editor::HyperlinkTooltipRuleViewModel RequestAddRule();

    private:
        Model::GlobalAppSettings _GlobalSettings;
        Model::WindowSettings _WindowSettings;
        Windows::Foundation::Collections::IObservableVector<Editor::HyperlinkTooltipRuleViewModel> _CurrentView;
        Windows::Foundation::Collections::IObservableVector<Editor::HyperlinkTooltipRuleViewModel>::VectorChanged_revoker _rulesChangedRevoker;
        Editor::HyperlinkTooltipRuleViewModel _CurrentRule{ nullptr };
    };
}

namespace winrt::Microsoft::Terminal::Settings::Editor::factory_implementation
{
    BASIC_FACTORY(HyperlinkTooltipActionViewModel);
    BASIC_FACTORY(HyperlinkTooltipRuleViewModel);
    BASIC_FACTORY(LinkTooltipViewModel);
}
