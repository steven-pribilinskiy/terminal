// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "LinkTooltipViewModel.h"
#include "LinkTooltipViewModel.g.cpp"
#include "HyperlinkTooltipRuleViewModel.g.cpp"
#include "HyperlinkTooltipActionViewModel.g.cpp"
#include "IntegrationChoiceViewModel.g.cpp"
#include "ButtonChoiceViewModel.g.cpp"
#include "EnumEntry.h"

using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Microsoft::Terminal::Settings::Model;

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    // Splits a comma-separated line into trimmed, non-empty tokens -- same convention
    // LinkTooltipViewModel::SafeUriSchemes already uses (moved here verbatim from
    // InteractionViewModel, since it's now needed by both that field and Schemes/
    // CustomExtensions below).
    static std::vector<winrt::hstring> _splitCommaList(const winrt::hstring& value)
    {
        std::vector<winrt::hstring> parts;
        for (const auto& part : til::split_iterator{ std::wstring_view{ value }, L',' })
        {
            if (const auto trimmed = til::trim(part, L' '); !trimmed.empty())
            {
                parts.emplace_back(trimmed);
            }
        }
        return parts;
    }

    static winrt::hstring _joinCommaList(const IVector<winrt::hstring>& values)
    {
        if (!values || values.Size() == 0)
        {
            return {};
        }

        std::wstring joined;
        for (const auto& value : values)
        {
            if (!joined.empty())
            {
                joined.append(L", ");
            }
            joined.append(value);
        }
        return winrt::hstring{ joined };
    }

    // The built-in card buttons, in the order the card draws them. The ids are
    // the ones stored in "hyperlink.tooltipButtons" and in a rule's "buttons".
    static constexpr std::wstring_view KnownButtonIds[]{
        L"open",
        L"copyLink",
        L"copyPath",
        L"reveal",
        L"showInPane",
    };

    static winrt::hstring _buttonLabel(const std::wstring_view id)
    {
        if (id == L"open")
        {
            return RS_(L"LinkTooltip_ButtonOpen");
        }
        if (id == L"copyLink")
        {
            return RS_(L"LinkTooltip_ButtonCopyLink");
        }
        if (id == L"copyPath")
        {
            return RS_(L"LinkTooltip_ButtonCopyPath");
        }
        if (id == L"reveal")
        {
            return RS_(L"LinkTooltip_ButtonReveal");
        }
        if (id == L"showInPane")
        {
            return RS_(L"LinkTooltip_ButtonShowInPane");
        }
        return winrt::hstring{ id };
    }

    static bool _listContains(const IVector<winrt::hstring>& list, const winrt::hstring& id)
    {
        if (!list)
        {
            return false;
        }
        for (const auto& value : list)
        {
            if (value == id)
            {
                return true;
            }
        }
        return false;
    }

    static bool _isKnownButtonId(const winrt::hstring& id)
    {
        return std::find(std::begin(KnownButtonIds), std::end(KnownButtonIds), std::wstring_view{ id }) != std::end(KnownButtonIds);
    }

    // Rebuilds an id list with one button turned on or off. The result is always
    // in KnownButtonIds order -- so the card draws them the same way however the
    // boxes were ticked -- and keeps any id we don't know about, which is how a
    // hand-written settings.json survives a round trip through this page.
    static IVector<winrt::hstring> _withButton(const IVector<winrt::hstring>& list, const winrt::hstring& id, bool selected)
    {
        std::vector<winrt::hstring> ordered;
        for (const auto& known : KnownButtonIds)
        {
            const winrt::hstring knownId{ known };
            const auto wanted = knownId == id ? selected : _listContains(list, knownId);
            if (wanted)
            {
                ordered.push_back(knownId);
            }
        }
        if (list)
        {
            for (const auto& value : list)
            {
                if (!_isKnownButtonId(value))
                {
                    ordered.push_back(value);
                }
            }
        }
        return single_threaded_vector<winrt::hstring>(std::move(ordered));
    }

    // What to show in a rule's "Integration" picker for a given id. "" and "none"
    // are not integration ids at all -- they're the two ways of saying "no specific
    // integration" -- so they get their own localized labels.
    static winrt::hstring _integrationDisplayName(const winrt::hstring& id)
    {
        if (id.empty())
        {
            return RS_(L"LinkTooltip_IntegrationAutomatic");
        }
        if (id == L"none")
        {
            return RS_(L"LinkTooltip_IntegrationNone");
        }
        return id;
    }

    static std::vector<Editor::IntegrationChoiceViewModel> _buildIntegrationChoices(const IVector<Model::HyperlinkTooltipRule>& rules)
    {
        std::vector<Editor::IntegrationChoiceViewModel> choices;
        choices.push_back(make<IntegrationChoiceViewModel>(winrt::hstring{}, _integrationDisplayName({})));
        choices.push_back(make<IntegrationChoiceViewModel>(winrt::hstring{ L"none" }, _integrationDisplayName(winrt::hstring{ L"none" })));
        if (const auto manifests = Model::IntegrationRegistry::All())
        {
            for (const auto& manifest : manifests)
            {
                const auto id = manifest.Id();
                if (id.empty())
                {
                    continue;
                }
                const auto name = manifest.Name();
                choices.push_back(make<IntegrationChoiceViewModel>(id, name.empty() ? id : name));
            }
        }
        if (rules)
        {
            for (const auto& rule : rules)
            {
                if (const auto current = rule.Integration(); !current.empty())
                {
                    const auto known = std::any_of(choices.begin(), choices.end(), [&](const auto& choice) { return choice.Id() == current; });
                    if (!known)
                    {
                        choices.push_back(make<IntegrationChoiceViewModel>(current, current));
                    }
                }
            }
        }
        return choices;
    }

    HyperlinkTooltipRuleViewModel::HyperlinkTooltipRuleViewModel(Model::HyperlinkTooltipRule rule,
                                                                 Model::WindowSettings windowSettings,
                                                                 IObservableVector<Editor::EnumEntry> kindList,
                                                                 IMap<Model::HyperlinkMatchKind, Editor::EnumEntry> kindMap,
                                                                 IObservableVector<Editor::EnumEntry> fileTypeGroupList,
                                                                 IMap<Model::HyperlinkFileTypeGroup, Editor::EnumEntry> fileTypeGroupMap,
                                                                 IObservableVector<Editor::IntegrationChoiceViewModel> integrationChoices) :
        _Rule{ rule },
        _WindowSettings{ windowSettings },
        _KindList{ kindList },
        _KindMap{ kindMap },
        _FileTypeGroupList{ fileTypeGroupList },
        _FileTypeGroupMap{ fileTypeGroupMap },
        _IntegrationChoices{ integrationChoices }
    {
        if (!_Rule.CustomActions())
        {
            _Rule.CustomActions(winrt::single_threaded_vector<Model::HyperlinkTooltipAction>());
        }

        // A rule with no list of its own inherits the global one, so the boxes
        // show what the card would actually do -- greyed out until the override
        // is switched on.
        std::vector<Editor::ButtonChoiceViewModel> buttonVMs;
        for (const auto& id : KnownButtonIds)
        {
            const winrt::hstring buttonId{ id };
            buttonVMs.push_back(make<ButtonChoiceViewModel>(
                buttonId,
                _buttonLabel(id),
                [this](const winrt::hstring& which) {
                    const auto own = _Rule.Buttons();
                    return _listContains(own && own.Size() > 0 ? own : _WindowSettings.HyperlinkTooltipButtons(), which);
                },
                [this](const winrt::hstring& which, bool selected) {
                    if (!OverrideButtons())
                    {
                        // The boxes are disabled while the rule inherits; don't
                        // let a stray write turn the override on by accident.
                        return;
                    }
                    const auto updated = _withButton(_Rule.Buttons(), which, selected);
                    if (updated.Size() == 0)
                    {
                        // An empty list is how a rule says "inherit", so it can't
                        // also mean "show nothing": refuse to clear the last box.
                        // ButtonChoiceViewModel re-reads Selected afterwards, so
                        // the checkbox snaps back on its own.
                        return;
                    }
                    _Rule.Buttons(updated);
                }));
        }
        _ButtonChoices = single_threaded_observable_vector<Editor::ButtonChoiceViewModel>(std::move(buttonVMs));

        std::vector<Editor::HyperlinkTooltipActionViewModel> actionVMs;
        for (const auto& action : _Rule.CustomActions())
        {
            actionVMs.push_back(make<HyperlinkTooltipActionViewModel>(action));
        }
        _CustomActions = single_threaded_observable_vector<Editor::HyperlinkTooltipActionViewModel>(std::move(actionVMs));

        _customActionsChangedRevoker = _CustomActions.VectorChanged(winrt::auto_revoke, [this](auto&&, const IVectorChangedEventArgs& args) {
            switch (args.CollectionChange())
            {
            case CollectionChange::Reset:
            {
                std::vector<Model::HyperlinkTooltipAction> modelActions;
                for (const auto& vm : _CustomActions)
                {
                    modelActions.push_back(get_self<HyperlinkTooltipActionViewModel>(vm)->Action());
                }
                _Rule.CustomActions(single_threaded_vector<Model::HyperlinkTooltipAction>(std::move(modelActions)));
                return;
            }
            case CollectionChange::ItemInserted:
            {
                const auto& vm = _CustomActions.GetAt(args.Index());
                _Rule.CustomActions().InsertAt(args.Index(), get_self<HyperlinkTooltipActionViewModel>(vm)->Action());
                return;
            }
            case CollectionChange::ItemRemoved:
                _Rule.CustomActions().RemoveAt(args.Index());
                return;
            case CollectionChange::ItemChanged:
            {
                const auto& vm = _CustomActions.GetAt(args.Index());
                _Rule.CustomActions().SetAt(args.Index(), get_self<HyperlinkTooltipActionViewModel>(vm)->Action());
                return;
            }
            }
        });
    }

    HyperlinkTooltipRuleViewModel::HyperlinkTooltipRuleViewModel(Model::HyperlinkTooltipRule rule, Model::WindowSettings windowSettings) :
        HyperlinkTooltipRuleViewModel(rule, windowSettings, nullptr, nullptr, nullptr, nullptr, nullptr)
    {
        INITIALIZE_BINDABLE_ENUM_SETTING(FileTypeGroup, HyperlinkFileTypeGroup, Model::HyperlinkFileTypeGroup, L"LinkTooltip_FileTypeGroup", L"Content");
        INITIALIZE_BINDABLE_ENUM_SETTING(Kind, HyperlinkMatchKind, Model::HyperlinkMatchKind, L"LinkTooltip_MatchKind", L"Content");
        auto choices = _buildIntegrationChoices(nullptr);
        if (const auto current = _Rule.Integration(); !current.empty())
        {
            const auto known = std::any_of(choices.begin(), choices.end(), [&](const auto& choice) { return choice.Id() == current; });
            if (!known)
            {
                choices.push_back(make<IntegrationChoiceViewModel>(current, current));
            }
        }
        _IntegrationChoices = single_threaded_observable_vector<Editor::IntegrationChoiceViewModel>(std::move(choices));
    }

    void HyperlinkTooltipRuleViewModel::Name(const hstring& value)
    {
        if (_Rule.Name() != value)
        {
            _Rule.Name(value);
            _NotifyChanges(L"Name", L"DisplayName");
        }
    }

    hstring HyperlinkTooltipRuleViewModel::DisplayName() const
    {
        const auto name = _Rule.Name();
        return !name.empty() ? name : RS_(L"LinkTooltip_NewRulePlaceholder");
    }

    void HyperlinkTooltipRuleViewModel::NotifySelectionProperties()
    {
        _NotifyChanges(L"CurrentKind", L"CurrentFileTypeGroup", L"CurrentIntegrationChoice");
    }

    winrt::Windows::Foundation::IInspectable HyperlinkTooltipRuleViewModel::CurrentKind() const
    {
        if (_KindMap && _KindMap.HasKey(_Rule.Kind()))
        {
            return winrt::box_value<Editor::EnumEntry>(_KindMap.Lookup(_Rule.Kind()));
        }
        return nullptr;
    }

    void HyperlinkTooltipRuleViewModel::CurrentKind(const winrt::Windows::Foundation::IInspectable& enumEntry)
    {
        if (auto ee = enumEntry.try_as<Editor::EnumEntry>())
        {
            auto setting = winrt::unbox_value<Model::HyperlinkMatchKind>(ee.EnumValue());
            if (_Rule.Kind() != setting)
            {
                _Rule.Kind(setting);
                _NotifyChanges(L"CurrentKind", L"IsLinkKind", L"IsTextKind", L"SummaryText");
            }
        }
    }

    winrt::Windows::Foundation::IInspectable HyperlinkTooltipRuleViewModel::CurrentFileTypeGroup() const
    {
        if (_FileTypeGroupMap && _FileTypeGroupMap.HasKey(_Rule.FileTypeGroup()))
        {
            return winrt::box_value<Editor::EnumEntry>(_FileTypeGroupMap.Lookup(_Rule.FileTypeGroup()));
        }
        return nullptr;
    }

    void HyperlinkTooltipRuleViewModel::CurrentFileTypeGroup(const winrt::Windows::Foundation::IInspectable& enumEntry)
    {
        if (auto ee = enumEntry.try_as<Editor::EnumEntry>())
        {
            auto setting = winrt::unbox_value<Model::HyperlinkFileTypeGroup>(ee.EnumValue());
            if (_Rule.FileTypeGroup() != setting)
            {
                _Rule.FileTypeGroup(setting);
                _NotifyChanges(L"CurrentFileTypeGroup", L"SummaryText");
            }
        }
    }

    winrt::Windows::Foundation::IInspectable HyperlinkTooltipRuleViewModel::CurrentIntegrationChoice() const
    {
        const auto current = _Rule.Integration();
        if (_IntegrationChoices)
        {
            for (const auto& choice : _IntegrationChoices)
            {
                if (choice.Id() == current)
                {
                    return choice;
                }
            }
            if (!current.empty())
            {
                const auto customChoice = make<IntegrationChoiceViewModel>(current, current);
                _IntegrationChoices.Append(customChoice);
                return customChoice;
            }
            if (_IntegrationChoices.Size() > 0)
            {
                return _IntegrationChoices.GetAt(0);
            }
        }
        return nullptr;
    }

    void HyperlinkTooltipRuleViewModel::CurrentIntegrationChoice(const winrt::Windows::Foundation::IInspectable& value)
    {
        if (!value)
        {
            return;
        }
        const auto choice = value.try_as<Editor::IntegrationChoiceViewModel>();
        if (!choice)
        {
            return;
        }
        _Rule.Integration(choice.Id());
        _NotifyChanges(L"Integration", L"CurrentIntegrationChoice", L"SummaryText");
    }

    hstring HyperlinkTooltipRuleViewModel::SummaryText() const
    {
        // A text rule matches terminal output rather than links, so none of the
        // link criteria below apply to it -- what matters is the pattern and who
        // previews what it finds.
        if (_Rule.Kind() == Model::HyperlinkMatchKind::Text)
        {
            std::wstring summary{ L"text: " };
            summary += std::wstring_view{ _Rule.Pattern() };
            summary += L"  →  ";
            summary += std::wstring_view{ _integrationDisplayName(_Rule.Integration()) };
            return winrt::hstring{ summary };
        }

        std::wstring parts;
        if (const auto schemes = _Rule.Schemes(); schemes && schemes.Size() > 0)
        {
            parts += L"scheme: ";
            parts += std::wstring_view{ _joinCommaList(schemes) };
        }
        if (const auto pattern = _Rule.Pattern(); !pattern.empty())
        {
            if (!parts.empty())
            {
                parts += L"  ·  ";
            }
            parts += L"pattern: ";
            parts += std::wstring_view{ pattern };
        }
        if (_Rule.FileTypeGroup() != Model::HyperlinkFileTypeGroup::None)
        {
            if (!parts.empty())
            {
                parts += L"  ·  ";
            }
            parts += L"file type";
        }
        return winrt::hstring{ parts };
    }

    hstring HyperlinkTooltipRuleViewModel::Schemes() const
    {
        return _joinCommaList(_Rule.Schemes());
    }

    void HyperlinkTooltipRuleViewModel::Schemes(const hstring& value)
    {
        auto parts = _splitCommaList(value);
        _Rule.Schemes(parts.empty() ? nullptr : single_threaded_vector<hstring>(std::move(parts)));
        _NotifyChanges(L"Schemes", L"SummaryText");
    }

    hstring HyperlinkTooltipRuleViewModel::CustomExtensions() const
    {
        return _joinCommaList(_Rule.CustomExtensions());
    }

    void HyperlinkTooltipRuleViewModel::CustomExtensions(const hstring& value)
    {
        auto parts = _splitCommaList(value);
        _Rule.CustomExtensions(parts.empty() ? nullptr : single_threaded_vector<hstring>(std::move(parts)));
        _NotifyChanges(L"CustomExtensions");
    }

    void HyperlinkTooltipRuleViewModel::OverrideShowDelay(bool value)
    {
        _Rule.TooltipShowDelay(value ? winrt::Windows::Foundation::IReference<int32_t>{ ShowDelay() } : nullptr);
        _NotifyChanges(L"OverrideShowDelay", L"ShowDelay");
    }

    int32_t HyperlinkTooltipRuleViewModel::ShowDelay() const noexcept
    {
        if (const auto value = _Rule.TooltipShowDelay())
        {
            return value.Value();
        }
        return 250;
    }

    void HyperlinkTooltipRuleViewModel::ShowDelay(int32_t value)
    {
        _Rule.TooltipShowDelay(winrt::Windows::Foundation::IReference<int32_t>{ value });
        _NotifyChanges(L"ShowDelay", L"OverrideShowDelay");
    }

    void HyperlinkTooltipRuleViewModel::OverrideHideDelay(bool value)
    {
        _Rule.TooltipHideDelay(value ? winrt::Windows::Foundation::IReference<int32_t>{ HideDelay() } : nullptr);
        _NotifyChanges(L"OverrideHideDelay", L"HideDelay");
    }

    int32_t HyperlinkTooltipRuleViewModel::HideDelay() const noexcept
    {
        if (const auto value = _Rule.TooltipHideDelay())
        {
            return value.Value();
        }
        return 400;
    }

    void HyperlinkTooltipRuleViewModel::HideDelay(int32_t value)
    {
        _Rule.TooltipHideDelay(winrt::Windows::Foundation::IReference<int32_t>{ value });
        _NotifyChanges(L"HideDelay", L"OverrideHideDelay");
    }

    void HyperlinkTooltipRuleViewModel::OverrideMaxWidth(bool value)
    {
        _Rule.TooltipMaxWidth(value ? winrt::Windows::Foundation::IReference<int32_t>{ MaxWidth() } : nullptr);
        _NotifyChanges(L"OverrideMaxWidth", L"MaxWidth");
    }

    int32_t HyperlinkTooltipRuleViewModel::MaxWidth() const noexcept
    {
        if (const auto value = _Rule.TooltipMaxWidth())
        {
            return value.Value();
        }
        return 640;
    }

    void HyperlinkTooltipRuleViewModel::MaxWidth(int32_t value)
    {
        _Rule.TooltipMaxWidth(winrt::Windows::Foundation::IReference<int32_t>{ value });
        _NotifyChanges(L"MaxWidth", L"OverrideMaxWidth");
    }

    bool HyperlinkTooltipRuleViewModel::OverrideButtons() const noexcept
    {
        const auto buttons = _Rule.Buttons();
        return buttons && buttons.Size() > 0;
    }

    void HyperlinkTooltipRuleViewModel::OverrideButtons(bool value)
    {
        if (OverrideButtons() == value)
        {
            return;
        }

        if (value)
        {
            // Start from what the rule was already inheriting, so switching the
            // override on doesn't silently change which buttons the card shows.
            std::vector<winrt::hstring> inherited;
            if (const auto defaults = _WindowSettings.HyperlinkTooltipButtons())
            {
                for (const auto& id : defaults)
                {
                    inherited.push_back(id);
                }
            }
            if (inherited.empty())
            {
                // There is nothing to copy, and an empty list would read as
                // "inherit" again -- so seed it with the one button that is
                // useful for every link.
                inherited.emplace_back(L"copyLink");
            }
            _Rule.Buttons(single_threaded_vector<winrt::hstring>(std::move(inherited)));
        }
        else
        {
            _Rule.Buttons(nullptr);
        }

        _NotifyChanges(L"OverrideButtons");
        _raiseButtonChoicesChanged();
    }

    void HyperlinkTooltipRuleViewModel::_raiseButtonChoicesChanged()
    {
        for (const auto& choice : _ButtonChoices)
        {
            get_self<ButtonChoiceViewModel>(choice)->RaiseSelectedChanged();
        }
    }

    void HyperlinkTooltipRuleViewModel::OverrideShowInPane(bool value)
    {
        _Rule.ShowInPane(value ? winrt::Windows::Foundation::IReference<bool>{ ShowInPane() } : nullptr);
        _NotifyChanges(L"OverrideShowInPane", L"ShowInPane");
    }

    bool HyperlinkTooltipRuleViewModel::ShowInPane() const noexcept
    {
        if (const auto value = _Rule.ShowInPane())
        {
            return value.Value();
        }
        return false;
    }

    void HyperlinkTooltipRuleViewModel::ShowInPane(bool value)
    {
        _Rule.ShowInPane(winrt::Windows::Foundation::IReference<bool>{ value });
        _NotifyChanges(L"ShowInPane", L"OverrideShowInPane");
    }

    Editor::HyperlinkTooltipActionViewModel HyperlinkTooltipRuleViewModel::RequestAddCustomAction()
    {
        Model::HyperlinkTooltipAction action{};
        const auto vm = make<HyperlinkTooltipActionViewModel>(action);
        _CustomActions.Append(vm);
        return vm;
    }

    void HyperlinkTooltipRuleViewModel::RequestDeleteCustomAction(const Editor::HyperlinkTooltipActionViewModel& vm)
    {
        uint32_t idx;
        if (_CustomActions.IndexOf(vm, idx))
        {
            _CustomActions.RemoveAt(idx);
        }
    }

    LinkTooltipViewModel::LinkTooltipViewModel(Model::GlobalAppSettings globalSettings, Model::WindowSettings windowSettings) :
        _GlobalSettings{ globalSettings },
        _WindowSettings{ windowSettings }
    {
        auto rules = _WindowSettings.HyperlinkTooltipRules();
        if (!rules)
        {
            rules = winrt::single_threaded_vector<Model::HyperlinkTooltipRule>();
        }
        // Assign unconditionally, not just when it's null: an inheritable setting
        // the user has never set hands back a *freshly constructed* fallback on
        // every read (see IInheritable.h), so a rule appended to what we just read
        // would land in a temporary and vanish. Assigning pins it as the user's own.
        _WindowSettings.HyperlinkTooltipRules(rules);

        std::vector<Editor::ButtonChoiceViewModel> buttonVMs;
        for (const auto& id : KnownButtonIds)
        {
            const winrt::hstring buttonId{ id };
            buttonVMs.push_back(make<ButtonChoiceViewModel>(
                buttonId,
                _buttonLabel(id),
                [this](const winrt::hstring& which) {
                    return _listContains(_WindowSettings.HyperlinkTooltipButtons(), which);
                },
                [this](const winrt::hstring& which, bool selected) {
                    _WindowSettings.HyperlinkTooltipButtons(_withButton(_WindowSettings.HyperlinkTooltipButtons(), which, selected));
                }));
        }
        _ButtonChoices = single_threaded_observable_vector<Editor::ButtonChoiceViewModel>(std::move(buttonVMs));

        INITIALIZE_BINDABLE_ENUM_SETTING(FileTypeGroup, HyperlinkFileTypeGroup, Model::HyperlinkFileTypeGroup, L"LinkTooltip_FileTypeGroup", L"Content");
        INITIALIZE_BINDABLE_ENUM_SETTING(Kind, HyperlinkMatchKind, Model::HyperlinkMatchKind, L"LinkTooltip_MatchKind", L"Content");
        _IntegrationChoices = single_threaded_observable_vector<Editor::IntegrationChoiceViewModel>(_buildIntegrationChoices(_WindowSettings.HyperlinkTooltipRules()));

        std::vector<Editor::HyperlinkTooltipRuleViewModel> ruleVMs;
        for (const auto& rule : _WindowSettings.HyperlinkTooltipRules())
        {
            ruleVMs.push_back(make<HyperlinkTooltipRuleViewModel>(rule, _WindowSettings, _KindList, _KindMap, _FileTypeGroupList, _FileTypeGroupMap, _IntegrationChoices));
        }
        _CurrentView = single_threaded_observable_vector<Editor::HyperlinkTooltipRuleViewModel>(std::move(ruleVMs));

        _rulesChangedRevoker = _CurrentView.VectorChanged(winrt::auto_revoke, [this](auto&&, const IVectorChangedEventArgs& args) {
            switch (args.CollectionChange())
            {
            case CollectionChange::Reset:
            {
                std::vector<Model::HyperlinkTooltipRule> modelRules;
                for (const auto& vm : _CurrentView)
                {
                    modelRules.push_back(get_self<HyperlinkTooltipRuleViewModel>(vm)->Rule());
                }
                _WindowSettings.HyperlinkTooltipRules(single_threaded_vector<Model::HyperlinkTooltipRule>(std::move(modelRules)));
                return;
            }
            case CollectionChange::ItemInserted:
            {
                const auto& vm = _CurrentView.GetAt(args.Index());
                _WindowSettings.HyperlinkTooltipRules().InsertAt(args.Index(), get_self<HyperlinkTooltipRuleViewModel>(vm)->Rule());
                return;
            }
            case CollectionChange::ItemRemoved:
                _WindowSettings.HyperlinkTooltipRules().RemoveAt(args.Index());
                return;
            case CollectionChange::ItemChanged:
            {
                const auto& vm = _CurrentView.GetAt(args.Index());
                _WindowSettings.HyperlinkTooltipRules().SetAt(args.Index(), get_self<HyperlinkTooltipRuleViewModel>(vm)->Rule());
                return;
            }
            }
        });
    }

    winrt::hstring LinkTooltipViewModel::SafeUriSchemes() const
    {
        return _joinCommaList(_WindowSettings.SafeUriSchemes());
    }

    void LinkTooltipViewModel::SafeUriSchemes(const winrt::hstring& value)
    {
        auto schemes = _splitCommaList(value);

        // An empty box clears the setting rather than storing an empty list, so the JSON
        // goes back to not mentioning it at all and the default applies again.
        if (schemes.empty())
        {
            _WindowSettings.ClearSafeUriSchemes();
        }
        else
        {
            _WindowSettings.SafeUriSchemes(winrt::single_threaded_vector<winrt::hstring>(std::move(schemes)));
        }

        _NotifyChanges(L"SafeUriSchemes");
    }

    // Opening or closing a rule is what swaps the page between the list and the
    // editor, and it is also what MainPage watches to push (or pop) the
    // "Link Tooltip > <rule>" breadcrumb. Renaming the open rule has to reach
    // the breadcrumb too, hence the subscription.
    void LinkTooltipViewModel::CurrentRule(const Editor::HyperlinkTooltipRuleViewModel& vm)
    {
        _currentRuleChangedRevoker.revoke();
        _CurrentRule = vm;
        if (_CurrentRule)
        {
            _currentRuleChangedRevoker = _CurrentRule.PropertyChanged(winrt::auto_revoke, [this](auto&&, const Windows::UI::Xaml::Data::PropertyChangedEventArgs& args) {
                if (args.PropertyName() == L"Name" || args.PropertyName() == L"DisplayName")
                {
                    _NotifyChanges(L"CurrentRuleName");
                }
            });
        }
        _NotifyChanges(L"CurrentRule", L"IsEditingRule", L"IsNotEditingRule", L"CurrentRuleName");
        if (_CurrentRule)
        {
            if (auto self = get_self<HyperlinkTooltipRuleViewModel>(_CurrentRule))
            {
                self->NotifySelectionProperties();
            }
        }
    }

    void LinkTooltipViewModel::RequestReorderRule(const Editor::HyperlinkTooltipRuleViewModel& vm, bool goingUp)
    {
        uint32_t idx;
        if (CurrentView().IndexOf(vm, idx))
        {
            if (goingUp && idx > 0)
            {
                CurrentView().RemoveAt(idx);
                CurrentView().InsertAt(idx - 1, vm);
            }
            else if (!goingUp && idx < CurrentView().Size() - 1)
            {
                CurrentView().RemoveAt(idx);
                CurrentView().InsertAt(idx + 1, vm);
            }
        }
    }

    void LinkTooltipViewModel::RequestDeleteRule(const Editor::HyperlinkTooltipRuleViewModel& vm)
    {
        uint32_t idx;
        if (CurrentView().IndexOf(vm, idx))
        {
            CurrentView().RemoveAt(idx);
            if (_CurrentRule == vm)
            {
                CurrentRule(nullptr);
            }
        }
    }

    Editor::HyperlinkTooltipRuleViewModel LinkTooltipViewModel::RequestAddRule()
    {
        Model::HyperlinkTooltipRule rule{};
        rule.Enabled(true);
        rule.CustomActions(winrt::single_threaded_vector<Model::HyperlinkTooltipAction>());
        const auto vm = make<HyperlinkTooltipRuleViewModel>(rule, _WindowSettings, _KindList, _KindMap, _FileTypeGroupList, _FileTypeGroupMap, _IntegrationChoices);
        CurrentView().Append(vm);
        return vm;
    }
}
