// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "LinkTooltipViewModel.h"
#include "LinkTooltipViewModel.g.cpp"
#include "RuleGroupViewModel.g.cpp"
#include "HyperlinkTooltipRuleViewModel.g.cpp"
#include "HyperlinkTooltipActionViewModel.g.cpp"
#include "IntegrationChoiceViewModel.g.cpp"
#include "ButtonChoiceViewModel.g.cpp"
#include "EnumEntry.h"
#include "LinkTooltipPresets.h"

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

    // The link kinds hyperlink.clickableKinds can name, in the order the
    // checkboxes are drawn.
    static constexpr std::wstring_view KnownClickableKindIds[]{
        L"detected",
        L"rules",
        L"osc8",
    };

    static winrt::hstring _clickableKindLabel(const std::wstring_view id)
    {
        if (id == L"detected")
        {
            return RS_(L"LinkTooltip_ClickableKindDetected");
        }
        if (id == L"rules")
        {
            return RS_(L"LinkTooltip_ClickableKindRules");
        }
        if (id == L"osc8")
        {
            return RS_(L"LinkTooltip_ClickableKindOsc8");
        }
        return winrt::hstring{ id };
    }

    // What a click chord can be bound to: the same ids the card's buttons use,
    // plus "none". `includeInherit` adds a leading "" entry, which only makes
    // sense on a rule -- the global setting has nothing to inherit from.
    static std::vector<Editor::IntegrationChoiceViewModel> _buildActionChoices(const bool includeInherit)
    {
        std::vector<Editor::IntegrationChoiceViewModel> choices;
        if (includeInherit)
        {
            choices.push_back(winrt::make<IntegrationChoiceViewModel>(winrt::hstring{}, RS_(L"LinkTooltip_ActionInherit")));
        }
        choices.push_back(winrt::make<IntegrationChoiceViewModel>(winrt::hstring{ L"none" }, RS_(L"LinkTooltip_ActionNone")));
        for (const auto& id : KnownButtonIds)
        {
            choices.push_back(winrt::make<IntegrationChoiceViewModel>(winrt::hstring{ id }, _buttonLabel(id)));
        }
        return choices;
    }

    // An id already in use that isn't one of the built-ins -- an entry under
    // "actions", say -- needs an entry of its own, or the picker would show
    // nothing selected and the first edit would silently rewrite the setting.
    // Done once while building the list rather than lazily on lookup: the rule's
    // list is a plain IVector, so a later Append would never reach the ComboBox.
    static void _ensureActionChoice(const IVector<Editor::IntegrationChoiceViewModel>& choices, const winrt::hstring& id)
    {
        if (!choices || id.empty())
        {
            return;
        }
        for (const auto& choice : choices)
        {
            if (choice.Id() == id)
            {
                return;
            }
        }
        choices.Append(winrt::make<IntegrationChoiceViewModel>(id, id));
    }

    // IMap::Lookup throws hresult_out_of_bounds when the key is absent, and these
    // maps are read from x:Bind getters that XAML calls while dispatching
    // PropertyChanged. An exception there does not surface as a failed binding -- it
    // escapes the callback and fails the process fast (0xC000041D,
    // STATUS_FATAL_USER_CALLBACK_EXCEPTION), taking every window with it. A combo box
    // showing nothing selected is the right failure for a value the list does not
    // contain.
    template<typename K>
    static Windows::Foundation::IInspectable _lookupEnumEntry(const IMap<K, Editor::EnumEntry>& map, const K& key)
    {
        if (!map || !map.HasKey(key))
        {
            return nullptr;
        }
        return map.Lookup(key);
    }

    // Shared by the four action pickers: pure lookup, since a binding getter is
    // no place to be mutating the collection the binding reads.
    static Windows::Foundation::IInspectable _actionChoiceFor(const IVector<Editor::IntegrationChoiceViewModel>& choices,
                                                              const winrt::hstring& current)
    {
        if (!choices)
        {
            return nullptr;
        }
        for (const auto& choice : choices)
        {
            if (choice.Id() == current)
            {
                return choice;
            }
        }
        return nullptr;
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

    // Toggles one id in a list, rewriting it in `known` order. The same shape as
    // _withButton, but over whichever id table is passed in -- the clickable-kind
    // checkboxes need this behaviour over a different set of ids, and unlike the
    // button list they have no unknown ids to preserve.
    template<typename KnownIds>
    static IVector<winrt::hstring> _withIdToggled(const IVector<winrt::hstring>& list,
                                                  const KnownIds& known,
                                                  const winrt::hstring& id,
                                                  bool selected)
    {
        std::vector<winrt::hstring> ordered;
        for (const auto& entry : known)
        {
            const winrt::hstring knownId{ entry };
            const auto wanted = knownId == id ? selected : _listContains(list, knownId);
            if (wanted)
            {
                ordered.push_back(knownId);
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
        for (const auto& preset : GetLinkTooltipPresets())
        {
            if (!preset.integration.empty())
            {
                const winrt::hstring presetId{ preset.integration };
                const auto known = std::any_of(choices.begin(), choices.end(), [&](const auto& choice) { return choice.Id() == presetId; });
                if (!known)
                {
                    choices.push_back(make<IntegrationChoiceViewModel>(presetId, _integrationDisplayName(presetId)));
                }
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
                // Weak, not [this]: these live inside ButtonChoiceViewModels that
                // XAML holds through its ItemsSource, so they outlive this view
                // model whenever the page is torn down or rebuilt while a box is
                // still bound. A raw this would be dangling by then, and the
                // read happens during teardown.
                [weakThis = winrt::weak_ref<HyperlinkTooltipRuleViewModel>{ get_weak() }](const winrt::hstring& which) {
                    const auto self = weakThis.get();
                    if (!self)
                    {
                        return false;
                    }
                    const auto own = self->_Rule.Buttons();
                    return _listContains(own && own.Size() > 0 ? own : self->_WindowSettings.HyperlinkTooltipButtons(), which);
                },
                [weakThis = winrt::weak_ref<HyperlinkTooltipRuleViewModel>{ get_weak() }](const winrt::hstring& which, bool selected) {
                    const auto self = weakThis.get();
                    if (!self)
                    {
                        return;
                    }
                    if (!self->OverrideButtons())
                    {
                        // The boxes are disabled while the rule inherits; don't
                        // let a stray write turn the override on by accident.
                        return;
                    }
                    const auto updated = _withButton(self->_Rule.Buttons(), which, selected);
                    if (updated.Size() == 0)
                    {
                        // An empty list is how a rule says "inherit", so it can't
                        // also mean "show nothing": refuse to clear the last box.
                        // ButtonChoiceViewModel re-reads Selected afterwards, so
                        // the checkbox snaps back on its own.
                        return;
                    }
                    self->_Rule.Buttons(updated);
                }));
        }
        _ButtonChoices = single_threaded_observable_vector<Editor::ButtonChoiceViewModel>(std::move(buttonVMs));

        // A rule's action pickers carry an extra leading "inherit" entry the
        // global ones don't have, so this list can't be shared with the page's.
        // Built here rather than in the delegating constructor, because this is
        // the one every rule actually goes through.
        _ActionChoices = single_threaded_vector<Editor::IntegrationChoiceViewModel>(_buildActionChoices(true));
        _ensureActionChoice(_ActionChoices, _Rule.PrimaryAction());
        _ensureActionChoice(_ActionChoices, _Rule.AlternativeAction());

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

    Windows::Foundation::IInspectable HyperlinkTooltipRuleViewModel::CurrentPrimaryAction() const
    {
        return _actionChoiceFor(_ActionChoices, _Rule.PrimaryAction());
    }

    void HyperlinkTooltipRuleViewModel::CurrentPrimaryAction(const Windows::Foundation::IInspectable& value)
    {
        if (const auto choice = value.try_as<Editor::IntegrationChoiceViewModel>(); choice && _Rule.PrimaryAction() != choice.Id())
        {
            _Rule.PrimaryAction(choice.Id());
            _NotifyChanges(L"CurrentPrimaryAction");
        }
    }

    Windows::Foundation::IInspectable HyperlinkTooltipRuleViewModel::CurrentAlternativeAction() const
    {
        return _actionChoiceFor(_ActionChoices, _Rule.AlternativeAction());
    }

    void HyperlinkTooltipRuleViewModel::CurrentAlternativeAction(const Windows::Foundation::IInspectable& value)
    {
        if (const auto choice = value.try_as<Editor::IntegrationChoiceViewModel>(); choice && _Rule.AlternativeAction() != choice.Id())
        {
            _Rule.AlternativeAction(choice.Id());
            _NotifyChanges(L"CurrentAlternativeAction");
        }
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
                _NotifyChanges(L"IsLinkKind", L"IsTextKind", L"SummaryText");
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
                _NotifyChanges(L"SummaryText");
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
        if (_Rule.Integration() != choice.Id())
        {
            _Rule.Integration(choice.Id());
            _NotifyChanges(L"Integration", L"SummaryText");
        }
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

    void HyperlinkTooltipRuleViewModel::ApplyPreset(const winrt::hstring& presetId)
    {
        const auto preset = FindLinkTooltipPreset(presetId);
        if (!preset)
        {
            return;
        }

        _Rule.Name(winrt::hstring{ preset->name });
        _Rule.Enabled(true);
        _Rule.Kind(preset->kind);
        _Rule.Pattern(winrt::hstring{ preset->pattern });
        if (!preset->schemes.empty())
        {
            std::vector<winrt::hstring> schemes;
            for (const auto& s : preset->schemes)
            {
                schemes.emplace_back(s);
            }
            _Rule.Schemes(winrt::single_threaded_vector<winrt::hstring>(std::move(schemes)));
        }
        else
        {
            _Rule.Schemes(nullptr);
        }
        _Rule.FileTypeGroup(preset->fileTypeGroup);
        if (!preset->customExtensions.empty())
        {
            std::vector<winrt::hstring> exts;
            for (const auto& e : preset->customExtensions)
            {
                exts.emplace_back(e);
            }
            _Rule.CustomExtensions(winrt::single_threaded_vector<winrt::hstring>(std::move(exts)));
        }
        else
        {
            _Rule.CustomExtensions(nullptr);
        }
        _Rule.Integration(winrt::hstring{ preset->integration });
        if (!preset->integration.empty() && _IntegrationChoices)
        {
            const winrt::hstring intId{ preset->integration };
            const auto known = std::any_of(begin(_IntegrationChoices), end(_IntegrationChoices), [&](const auto& choice) {
                return choice.Id() == intId;
            });
            if (!known)
            {
                _IntegrationChoices.Append(make<IntegrationChoiceViewModel>(intId, _integrationDisplayName(intId)));
            }
        }
        _Rule.ShowPreview(preset->showPreview);
        _Rule.TooltipShowDelay(nullptr);
        _Rule.TooltipHideDelay(nullptr);
        _Rule.TooltipMaxWidth(nullptr);
        _Rule.Buttons(nullptr);
        _Rule.ShowInPane(nullptr);

        _NotifyChanges(L"Name",
                       L"DisplayName",
                       L"Enabled",
                       L"Pattern",
                       L"CurrentKind",
                       L"IsLinkKind",
                       L"IsTextKind",
                       L"Schemes",
                       L"CurrentFileTypeGroup",
                       L"CustomExtensions",
                       L"Integration",
                       L"CurrentIntegrationChoice",
                       L"ShowPreview",
                       L"SummaryText",
                       L"OverrideShowDelay",
                       L"ShowDelay",
                       L"OverrideHideDelay",
                       L"HideDelay",
                       L"OverrideMaxWidth",
                       L"MaxWidth",
                       L"OverrideButtons",
                       L"OverrideShowInPane",
                       L"ShowInPane");
    }

    RuleGroupViewModel::RuleGroupViewModel(winrt::hstring platformId,
                                           winrt::hstring platformName,
                                           winrt::hstring platformIcon,
                                           std::vector<Editor::HyperlinkTooltipRuleViewModel> rules) :
        _platformId{ std::move(platformId) },
        _platformName{ std::move(platformName) },
        _platformIcon{ std::move(platformIcon) },
        _rules{ winrt::single_threaded_observable_vector<Editor::HyperlinkTooltipRuleViewModel>(std::move(rules)) }
    {
        for (const auto& r : _rules)
        {
            winrt::weak_ref<RuleGroupViewModel> weakThis{ get_weak() };
            r.PropertyChanged([weakThis](auto&&, const Windows::UI::Xaml::Data::PropertyChangedEventArgs& args) {
                if (auto strong = weakThis.get())
                {
                    if (args.PropertyName() == L"Enabled")
                    {
                        strong->RefreshState();
                    }
                }
            });
        }
    }

    winrt::hstring RuleGroupViewModel::CountBadge() const
    {
        if (!_rules)
        {
            return winrt::hstring{ L"0/0" };
        }
        uint32_t enabled = 0;
        const auto total = _rules.Size();
        for (const auto& r : _rules)
        {
            if (r.Enabled())
            {
                enabled++;
            }
        }
        return winrt::hstring{ fmt::format(L"{}/{}", enabled, total) };
    }

    bool RuleGroupViewModel::IsEnabled() const
    {
        if (!_rules || _rules.Size() == 0)
        {
            return false;
        }
        for (const auto& r : _rules)
        {
            if (r.Enabled())
            {
                return true;
            }
        }
        return false;
    }

    void RuleGroupViewModel::IsEnabled(bool value)
    {
        if (!_rules)
        {
            return;
        }
        for (const auto& r : _rules)
        {
            r.Enabled(value);
        }
        RefreshState();
    }

    void RuleGroupViewModel::RefreshState()
    {
        _NotifyChanges(L"IsEnabled", L"CountBadge");
    }

    static IVector<Model::HyperlinkTooltipRule> _ensureRules(const Model::WindowSettings& windowSettings)
    {
        auto rules = windowSettings.HyperlinkTooltipRules();
        if (!rules)
        {
            rules = winrt::single_threaded_vector<Model::HyperlinkTooltipRule>();
            windowSettings.HyperlinkTooltipRules(rules);
        }
        return rules;
    }

    static std::wstring _toLower(std::wstring_view s)
    {
        std::wstring res{ s };
        std::transform(res.begin(), res.end(), res.begin(), ::towlower);
        return res;
    }

    static winrt::hstring _classifyRulePlatform(const Editor::HyperlinkTooltipRuleViewModel& vm)
    {
        const auto name = _toLower(vm.DisplayName());
        const auto pattern = _toLower(vm.Pattern());
        const auto integration = _toLower(vm.Integration());
        const auto schemes = _toLower(vm.Schemes());

        if (integration.find(L"github") != std::wstring::npos ||
            name.find(L"github") != std::wstring::npos ||
            pattern.find(L"github") != std::wstring::npos)
        {
            return L"github";
        }
        if (integration.find(L"jira") != std::wstring::npos ||
            name.find(L"jira") != std::wstring::npos ||
            pattern.find(L"jira") != std::wstring::npos ||
            pattern.find(L"browse/") != std::wstring::npos)
        {
            return L"jira";
        }
        if (integration.find(L"slack") != std::wstring::npos ||
            name.find(L"slack") != std::wstring::npos ||
            pattern.find(L"slack") != std::wstring::npos)
        {
            return L"slack";
        }
        if (integration.find(L"stith") != std::wstring::npos ||
            name.find(L"stith") != std::wstring::npos)
        {
            return L"stith";
        }
        if (name.find(L"git") != std::wstring::npos ||
            name.find(L"commit") != std::wstring::npos ||
            pattern.find(L"commit") != std::wstring::npos ||
            schemes.find(L"git") != std::wstring::npos)
        {
            return L"git";
        }
        if (schemes.find(L"file") != std::wstring::npos ||
            name.find(L"file") != std::wstring::npos ||
            name.find(L"markdown") != std::wstring::npos ||
            name.find(L"media") != std::wstring::npos ||
            name.find(L"path") != std::wstring::npos)
        {
            return L"files";
        }
        return L"custom";
    }

    // The order _updateRuleGroups draws the groups in, and therefore the order
    // automatic sorting puts the rules in. "custom" is last, so ungrouped rules
    // sit below the grouped ones -- and, since order is precedence, lose to them.
    static constexpr std::wstring_view GroupOrder[]{
        L"github",
        L"jira",
        L"slack",
        L"stith",
        L"git",
        L"files",
        L"custom",
    };

    static size_t _groupRank(const winrt::hstring& platform)
    {
        for (size_t i = 0; i < std::size(GroupOrder); ++i)
        {
            if (GroupOrder[i] == platform)
            {
                return i;
            }
        }
        return std::size(GroupOrder);
    }

    LinkTooltipViewModel::LinkTooltipViewModel(Model::GlobalAppSettings globalSettings, Model::WindowSettings windowSettings) :
        _GlobalSettings{ globalSettings },
        _WindowSettings{ windowSettings }
    {
        auto rules = _ensureRules(_WindowSettings);

        std::vector<Editor::ButtonChoiceViewModel> buttonVMs;
        for (const auto& id : KnownButtonIds)
        {
            const winrt::hstring buttonId{ id };
            buttonVMs.push_back(make<ButtonChoiceViewModel>(
                buttonId,
                _buttonLabel(id),
                // The settings object, not [this]: these outlive the view model
                // whenever XAML still holds the ButtonChoiceViewModels through its
                // ItemsSource. _WindowSettings is set once in the constructor and
                // never reassigned, so a captured copy is the same object -- and it
                // keeps itself alive.
                [windowSettings = _WindowSettings](const winrt::hstring& which) {
                    return _listContains(windowSettings.HyperlinkTooltipButtons(), which);
                },
                [windowSettings = _WindowSettings](const winrt::hstring& which, bool selected) {
                    windowSettings.HyperlinkTooltipButtons(_withButton(windowSettings.HyperlinkTooltipButtons(), which, selected));
                }));
        }
        _ButtonChoices = single_threaded_observable_vector<Editor::ButtonChoiceViewModel>(std::move(buttonVMs));

        INITIALIZE_BINDABLE_ENUM_SETTING(FileTypeGroup, HyperlinkFileTypeGroup, Model::HyperlinkFileTypeGroup, L"LinkTooltip_FileTypeGroup", L"Content");
        INITIALIZE_BINDABLE_ENUM_SETTING(Kind, HyperlinkMatchKind, Model::HyperlinkMatchKind, L"LinkTooltip_MatchKind", L"Content");
        INITIALIZE_BINDABLE_ENUM_SETTING(IntegrationDisplayMode, HyperlinkIntegrationDisplayMode, Model::HyperlinkIntegrationDisplayMode, L"LinkTooltip_IntegrationDisplayMode", L"Content");
        INITIALIZE_BINDABLE_ENUM_SETTING(ActionPlacement, HyperlinkActionPlacement, Model::HyperlinkActionPlacement, L"LinkTooltip_ActionPlacement", L"Content");
        INITIALIZE_BINDABLE_ENUM_SETTING(ClickModifier, HyperlinkClickModifier, Model::HyperlinkClickModifier, L"LinkTooltip_ClickModifier", L"Content");
        INITIALIZE_BINDABLE_ENUM_SETTING(ClickGesture, HyperlinkClickGesture, Model::HyperlinkClickGesture, L"LinkTooltip_ClickGesture", L"Content");
        _IntegrationChoices = single_threaded_observable_vector<Editor::IntegrationChoiceViewModel>(_buildIntegrationChoices(rules));
        _ActionChoices = single_threaded_observable_vector<Editor::IntegrationChoiceViewModel>(_buildActionChoices(false));
        _ensureActionChoice(_ActionChoices, _WindowSettings.HyperlinkPrimaryAction());
        _ensureActionChoice(_ActionChoices, _WindowSettings.HyperlinkAlternativeAction());

        std::vector<Editor::ButtonChoiceViewModel> kindVMs;
        for (const auto& id : KnownClickableKindIds)
        {
            const winrt::hstring kindId{ id };
            kindVMs.push_back(make<ButtonChoiceViewModel>(
                kindId,
                _clickableKindLabel(id),
                [windowSettings = _WindowSettings](const winrt::hstring& which) {
                    // An unset list is the shipped default -- every kind -- rather
                    // than "no kinds", matching how the control resolves it.
                    const auto kinds = windowSettings.HyperlinkClickableKinds();
                    return !kinds ? true : _listContains(kinds, which);
                },
                [windowSettings = _WindowSettings](const winrt::hstring& which, bool selected) {
                    auto kinds = windowSettings.HyperlinkClickableKinds();
                    if (!kinds)
                    {
                        kinds = single_threaded_vector<winrt::hstring>();
                        for (const auto& id : KnownClickableKindIds)
                        {
                            kinds.Append(winrt::hstring{ id });
                        }
                    }
                    windowSettings.HyperlinkClickableKinds(_withIdToggled(kinds, KnownClickableKindIds, which, selected));
                }));
        }
        _ClickableKindChoices = single_threaded_observable_vector<Editor::ButtonChoiceViewModel>(std::move(kindVMs));

        std::vector<Editor::HyperlinkTooltipRuleViewModel> ruleVMs;
        for (const auto& rule : rules)
        {
            ruleVMs.push_back(make<HyperlinkTooltipRuleViewModel>(rule, _WindowSettings, _KindList, _KindMap, _FileTypeGroupList, _FileTypeGroupMap, _IntegrationChoices));
        }
        _CurrentView = single_threaded_observable_vector<Editor::HyperlinkTooltipRuleViewModel>(std::move(ruleVMs));
        _RuleGroups = single_threaded_observable_vector<Editor::RuleGroupViewModel>();
        _updateRuleGroups();

        _rulesChangedRevoker = _CurrentView.VectorChanged(winrt::auto_revoke, [this](auto&&, const IVectorChangedEventArgs& args) {
            auto rules = _ensureRules(_WindowSettings);

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
                break;
            }
            case CollectionChange::ItemInserted:
            {
                const auto& vm = _CurrentView.GetAt(args.Index());
                rules.InsertAt(args.Index(), get_self<HyperlinkTooltipRuleViewModel>(vm)->Rule());
                break;
            }
            case CollectionChange::ItemRemoved:
                if (args.Index() < rules.Size())
                {
                    rules.RemoveAt(args.Index());
                }
                break;
            case CollectionChange::ItemChanged:
            {
                const auto& vm = _CurrentView.GetAt(args.Index());
                if (args.Index() < rules.Size())
                {
                    rules.SetAt(args.Index(), get_self<HyperlinkTooltipRuleViewModel>(vm)->Rule());
                }
                break;
            }
            }
            _updateRuleGroups();
        });

        // Only now that the write-through above is armed, so the sort reaches
        // hyperlink.tooltipRules rather than only the list on screen.
        _applyAutomaticOrder();
    }

    // With manual ordering off, the list you see IS the precedence order rather
    // than a display that lies about it: the model is kept sorted by group rank
    // (Custom Rules last) and then by name. Turning manual ordering on simply
    // stops this running, freezing the current order as the starting point.
    void LinkTooltipViewModel::_applyAutomaticOrder()
    {
        if (!_CurrentView || _reorderingInProgress || _WindowSettings.HyperlinkManualRuleOrder())
        {
            return;
        }

        std::vector<Editor::HyperlinkTooltipRuleViewModel> sorted{ begin(_CurrentView), end(_CurrentView) };
        std::stable_sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) {
            const auto lhsRank = _groupRank(_classifyRulePlatform(lhs));
            const auto rhsRank = _groupRank(_classifyRulePlatform(rhs));
            if (lhsRank != rhsRank)
            {
                return lhsRank < rhsRank;
            }
            return _toLower(lhs.DisplayName()) < _toLower(rhs.DisplayName());
        });

        // Already in order. Returning without touching the vector is what stops
        // the VectorChanged handler below from calling back in here forever --
        // see the stack overflow this page has already had once.
        if (std::equal(sorted.begin(), sorted.end(), begin(_CurrentView), end(_CurrentView)))
        {
            return;
        }

        _reorderingInProgress = true;
        _CurrentView.ReplaceAll(sorted);
        _reorderingInProgress = false;
    }

    void LinkTooltipViewModel::_updateRuleGroups()
    {
        if (!_RuleGroups)
        {
            _RuleGroups = winrt::single_threaded_observable_vector<Editor::RuleGroupViewModel>();
        }

        struct GroupInfo
        {
            std::wstring id;
            std::wstring name;
            std::wstring icon;
            std::vector<Editor::HyperlinkTooltipRuleViewModel> rules;
        };

        std::vector<GroupInfo> groups = {
            { L"github", L"GitHub", L"\uE82D", {} },
            { L"jira", L"Jira", L"\uE943", {} },
            { L"slack", L"Slack", L"\uE8BD", {} },
            { L"stith", L"Stith", L"\uE774", {} },
            { L"git", L"Git", L"\uE81D", {} },
            { L"files", L"Files & Media", L"\uE8A5", {} },
            { L"custom", L"Custom Rules", L"\uE713", {} }
        };

        if (_CurrentView)
        {
            for (const auto& vm : _CurrentView)
            {
                const auto platform = _classifyRulePlatform(vm);
                bool found = false;
                for (auto& g : groups)
                {
                    if (g.id == platform)
                    {
                        g.rules.push_back(vm);
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    groups.back().rules.push_back(vm);
                }
            }
        }

        _RuleGroups.Clear();
        for (auto& g : groups)
        {
            if (!g.rules.empty())
            {
                _RuleGroups.Append(winrt::make<RuleGroupViewModel>(
                    winrt::hstring{ g.id },
                    winrt::hstring{ g.name },
                    winrt::hstring{ g.icon },
                    std::move(g.rules)));
            }
        }
        _NotifyChanges(L"RuleGroups");
    }

    Windows::Foundation::IInspectable LinkTooltipViewModel::CurrentIntegrationDisplayMode() const
    {
        return _lookupEnumEntry(_IntegrationDisplayModeMap, _WindowSettings.HyperlinkIntegrationDisplayMode());
    }

    void LinkTooltipViewModel::CurrentIntegrationDisplayMode(const Windows::Foundation::IInspectable& enumEntry)
    {
        if (const auto entry = enumEntry.try_as<Editor::EnumEntry>())
        {
            const auto value = winrt::unbox_value<Model::HyperlinkIntegrationDisplayMode>(entry.EnumValue());
            _WindowSettings.HyperlinkIntegrationDisplayMode(value);
            _NotifyChanges(L"CurrentIntegrationDisplayMode", L"HyperlinkIntegrationDisplayMode");
        }
    }

    Windows::Foundation::IInspectable LinkTooltipViewModel::CurrentActionPlacement() const
    {
        return _lookupEnumEntry(_ActionPlacementMap, _WindowSettings.HyperlinkActionPlacement());
    }

    void LinkTooltipViewModel::CurrentActionPlacement(const Windows::Foundation::IInspectable& enumEntry)
    {
        if (const auto entry = enumEntry.try_as<Editor::EnumEntry>())
        {
            const auto value = winrt::unbox_value<Model::HyperlinkActionPlacement>(entry.EnumValue());
            _WindowSettings.HyperlinkActionPlacement(value);
            _NotifyChanges(L"CurrentActionPlacement", L"HyperlinkActionPlacement");
        }
    }

    Windows::Foundation::IInspectable LinkTooltipViewModel::CurrentPrimaryClickModifier() const
    {
        return _lookupEnumEntry(_ClickModifierMap, _WindowSettings.HyperlinkPrimaryClickModifier());
    }

    void LinkTooltipViewModel::CurrentPrimaryClickModifier(const Windows::Foundation::IInspectable& enumEntry)
    {
        if (const auto entry = enumEntry.try_as<Editor::EnumEntry>())
        {
            _WindowSettings.HyperlinkPrimaryClickModifier(winrt::unbox_value<Model::HyperlinkClickModifier>(entry.EnumValue()));
            _NotifyChanges(L"CurrentPrimaryClickModifier");
        }
    }

    Windows::Foundation::IInspectable LinkTooltipViewModel::CurrentPrimaryClickGesture() const
    {
        return _lookupEnumEntry(_ClickGestureMap, _WindowSettings.HyperlinkPrimaryClickGesture());
    }

    void LinkTooltipViewModel::CurrentPrimaryClickGesture(const Windows::Foundation::IInspectable& enumEntry)
    {
        if (const auto entry = enumEntry.try_as<Editor::EnumEntry>())
        {
            _WindowSettings.HyperlinkPrimaryClickGesture(winrt::unbox_value<Model::HyperlinkClickGesture>(entry.EnumValue()));
            _NotifyChanges(L"CurrentPrimaryClickGesture");
        }
    }

    Windows::Foundation::IInspectable LinkTooltipViewModel::CurrentAlternativeClickModifier() const
    {
        return _lookupEnumEntry(_ClickModifierMap, _WindowSettings.HyperlinkAlternativeClickModifier());
    }

    void LinkTooltipViewModel::CurrentAlternativeClickModifier(const Windows::Foundation::IInspectable& enumEntry)
    {
        if (const auto entry = enumEntry.try_as<Editor::EnumEntry>())
        {
            _WindowSettings.HyperlinkAlternativeClickModifier(winrt::unbox_value<Model::HyperlinkClickModifier>(entry.EnumValue()));
            _NotifyChanges(L"CurrentAlternativeClickModifier");
        }
    }

    Windows::Foundation::IInspectable LinkTooltipViewModel::CurrentAlternativeClickGesture() const
    {
        return _lookupEnumEntry(_ClickGestureMap, _WindowSettings.HyperlinkAlternativeClickGesture());
    }

    void LinkTooltipViewModel::CurrentAlternativeClickGesture(const Windows::Foundation::IInspectable& enumEntry)
    {
        if (const auto entry = enumEntry.try_as<Editor::EnumEntry>())
        {
            _WindowSettings.HyperlinkAlternativeClickGesture(winrt::unbox_value<Model::HyperlinkClickGesture>(entry.EnumValue()));
            _NotifyChanges(L"CurrentAlternativeClickGesture");
        }
    }

    Windows::Foundation::IInspectable LinkTooltipViewModel::CurrentPrimaryAction() const
    {
        return _actionChoiceFor(_ActionChoices, _WindowSettings.HyperlinkPrimaryAction());
    }

    void LinkTooltipViewModel::CurrentPrimaryAction(const Windows::Foundation::IInspectable& value)
    {
        if (const auto choice = value.try_as<Editor::IntegrationChoiceViewModel>())
        {
            _WindowSettings.HyperlinkPrimaryAction(choice.Id());
            _NotifyChanges(L"CurrentPrimaryAction");
        }
    }

    Windows::Foundation::IInspectable LinkTooltipViewModel::CurrentAlternativeAction() const
    {
        return _actionChoiceFor(_ActionChoices, _WindowSettings.HyperlinkAlternativeAction());
    }

    void LinkTooltipViewModel::CurrentAlternativeAction(const Windows::Foundation::IInspectable& value)
    {
        if (const auto choice = value.try_as<Editor::IntegrationChoiceViewModel>())
        {
            _WindowSettings.HyperlinkAlternativeAction(choice.Id());
            _NotifyChanges(L"CurrentAlternativeAction");
        }
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
        const auto wasEditing = static_cast<bool>(_CurrentRule);
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
        else if (wasEditing)
        {
            _updateRuleGroups();
        }
        _NotifyChanges(L"CurrentRule", L"IsEditingRule", L"IsNotEditingRule", L"CurrentRuleName");
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
        _ensureRules(_WindowSettings);
        Model::HyperlinkTooltipRule rule{};
        rule.Enabled(true);
        rule.CustomActions(winrt::single_threaded_vector<Model::HyperlinkTooltipAction>());
        const auto vm = make<HyperlinkTooltipRuleViewModel>(rule, _WindowSettings, _KindList, _KindMap, _FileTypeGroupList, _FileTypeGroupMap, _IntegrationChoices);
        CurrentView().Append(vm);
        // A blank rule classifies as Custom, which sorts last anyway, so this
        // leaves it at the end -- where the user is about to start editing it.
        _applyAutomaticOrder();
        return vm;
    }

    // Whether the rules list already contains this preset, so the Add rule menu can
    // grey it out instead of silently making a duplicate that matches the same links
    // twice. Rules do not record the preset they came from, so this compares what a
    // preset actually writes: the name, and the pattern where there is one. A preset
    // whose pattern is empty (the file-type ones) is identified by name and file-type
    // group, which is the only thing that distinguishes those from each other.
    bool LinkTooltipViewModel::IsPresetInUse(const winrt::hstring& presetId) const
    {
        const auto preset = FindLinkTooltipPreset(presetId);
        if (!preset)
        {
            return false;
        }

        const auto rules = _WindowSettings.HyperlinkTooltipRules();
        if (!rules)
        {
            return false;
        }

        const std::wstring_view presetName{ preset->name };
        const std::wstring_view presetPattern{ preset->pattern };

        for (const auto& rule : rules)
        {
            if (!rule)
            {
                continue;
            }

            if (!presetPattern.empty())
            {
                if (std::wstring_view{ rule.Pattern() } == presetPattern)
                {
                    return true;
                }
                continue;
            }

            if (std::wstring_view{ rule.Name() } == presetName && rule.FileTypeGroup() == preset->fileTypeGroup)
            {
                return true;
            }
        }
        return false;
    }

    void LinkTooltipViewModel::ExpandAllRuleGroups()
    {
        if (!_RuleGroups)
        {
            return;
        }
        for (const auto& group : _RuleGroups)
        {
            if (group)
            {
                group.IsExpanded(true);
            }
        }
    }

    void LinkTooltipViewModel::CollapseAllRuleGroups()
    {
        if (!_RuleGroups)
        {
            return;
        }
        for (const auto& group : _RuleGroups)
        {
            if (group)
            {
                group.IsExpanded(false);
            }
        }
    }

    Editor::HyperlinkTooltipRuleViewModel LinkTooltipViewModel::RequestAddRuleWithPreset(const winrt::hstring& presetId)
    {
        _ensureRules(_WindowSettings);
        if (const auto preset = FindLinkTooltipPreset(presetId))
        {
            const auto rule = CreateRuleFromPreset(*preset);
            const auto vm = make<HyperlinkTooltipRuleViewModel>(rule, _WindowSettings, _KindList, _KindMap, _FileTypeGroupList, _FileTypeGroupMap, _IntegrationChoices);
            CurrentView().Append(vm);
            _applyAutomaticOrder();
            return vm;
        }
        return RequestAddRule();
    }

    // The manual-ordering switch. Turning it off re-sorts at once, so the list
    // never sits in an order the setting says it does not have. This is written
    // out rather than left to PERMANENT_OBSERVABLE_PROJECTED_SETTING because of
    // that side effect.
    void LinkTooltipViewModel::HyperlinkManualRuleOrder(bool value)
    {
        if (_WindowSettings.HyperlinkManualRuleOrder() == value)
        {
            return;
        }
        _WindowSettings.HyperlinkManualRuleOrder(value);
        _applyAutomaticOrder();
        _NotifyChanges(L"HasHyperlinkManualRuleOrder", L"HyperlinkManualRuleOrder", L"IsAutomaticRuleOrder");
    }
}
