// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "LinkTooltipViewModel.h"
#include "LinkTooltipViewModel.g.cpp"
#include "HyperlinkTooltipRuleViewModel.g.cpp"
#include "HyperlinkTooltipActionViewModel.g.cpp"
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

    HyperlinkTooltipRuleViewModel::HyperlinkTooltipRuleViewModel(Model::HyperlinkTooltipRule rule) :
        _Rule{ rule }
    {
        if (!_Rule.CustomActions())
        {
            _Rule.CustomActions(winrt::single_threaded_vector<Model::HyperlinkTooltipAction>());
        }

        INITIALIZE_BINDABLE_ENUM_SETTING(FileTypeGroup, HyperlinkFileTypeGroup, Model::HyperlinkFileTypeGroup, L"LinkTooltip_FileTypeGroup", L"Content");

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

    hstring HyperlinkTooltipRuleViewModel::SummaryText() const
    {
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
        if (!_WindowSettings.HyperlinkTooltipRules())
        {
            _WindowSettings.HyperlinkTooltipRules(winrt::single_threaded_vector<Model::HyperlinkTooltipRule>());
        }

        std::vector<Editor::HyperlinkTooltipRuleViewModel> ruleVMs;
        for (const auto& rule : _WindowSettings.HyperlinkTooltipRules())
        {
            ruleVMs.push_back(make<HyperlinkTooltipRuleViewModel>(rule));
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
        const auto vm = make<HyperlinkTooltipRuleViewModel>(rule);
        CurrentView().Append(vm);
        return vm;
    }
}
