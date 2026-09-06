// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "LinkTooltip.h"
#include "LinkTooltip.g.cpp"
#include "LinkTooltipPresets.h"

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Navigation;
using namespace winrt::Microsoft::Terminal::Settings::Model;

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    LinkTooltip::LinkTooltip()
    {
        InitializeComponent();
    }

    void LinkTooltip::OnNavigatedTo(const NavigationEventArgs& e)
    {
        const auto args = e.Parameter().as<Editor::NavigateToPageArgs>();
        _ViewModel = args.ViewModel().as<Editor::LinkTooltipViewModel>();
        _weakWindowRoot = args.WindowRoot();
        BringIntoViewWhenLoaded(args.ElementToFocus());

        TraceLoggingWrite(
            g_hTerminalSettingsEditorProvider,
            "NavigatedToPage",
            TraceLoggingDescription("Event emitted when the user navigates to a page in the settings UI"),
            TraceLoggingValue("linkTooltip", "PageId", "The identifier of the page that was navigated to"),
            TraceLoggingKeyword(MICROSOFT_KEYWORD_MEASURES),
            TelemetryPrivacyDataTag(PDT_ProductAndServiceUsage));
    }

    void LinkTooltip::AddRuleButton_Click(const IInspectable& /*sender*/, const winrt::Microsoft::UI::Xaml::Controls::SplitButtonClickEventArgs& /*e*/)
    {
        const auto rule = _ViewModel.RequestAddRule();
        Dispatcher().RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, [weakThis{ get_weak() }, rule]() {
            if (const auto self{ weakThis.get() })
            {
                self->_ViewModel.CurrentRule(rule);
            }
        });
    }

    void LinkTooltip::ExpandAllRuleGroups_Click(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        _ViewModel.ExpandAllRuleGroups();
    }

    void LinkTooltip::CollapseAllRuleGroups_Click(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        _ViewModel.CollapseAllRuleGroups();
    }

    // The preset menu's shape, shared by "Add rule" and "Apply preset" so the two
    // cannot drift apart -- which they had, leaving Apply preset offering every
    // preset including the ones already in the list.
    struct PresetCategory
    {
        std::wstring name;
        std::vector<const LinkTooltipPreset*> presets;
    };

    static std::vector<PresetCategory> _categorizedPresets()
    {
        std::vector<PresetCategory> categories = {
            { L"GitHub", {} },
            { L"Jira", {} },
            { L"Slack", {} },
            { L"Stith", {} },
            { L"Git", {} },
            { L"Files & Media", {} },
            { L"General", {} }
        };

        for (const auto& preset : GetLinkTooltipPresets())
        {
            const std::wstring_view id{ preset.id };
            if (id.rfind(L"github", 0) == 0)
            {
                categories[0].presets.push_back(&preset);
            }
            else if (id.rfind(L"jira", 0) == 0)
            {
                categories[1].presets.push_back(&preset);
            }
            else if (id.rfind(L"slack", 0) == 0)
            {
                categories[2].presets.push_back(&preset);
            }
            else if (id.rfind(L"stith", 0) == 0)
            {
                categories[3].presets.push_back(&preset);
            }
            else if (id.rfind(L"git", 0) == 0)
            {
                categories[4].presets.push_back(&preset);
            }
            else if (id.rfind(L"file", 0) == 0)
            {
                categories[5].presets.push_back(&preset);
            }
            else
            {
                categories[6].presets.push_back(&preset);
            }
        }
        return categories;
    }

    void LinkTooltip::AddRuleFlyout_Opening(const IInspectable& sender, const IInspectable& /*args*/)
    {
        const auto flyout = sender.try_as<Controls::MenuFlyout>();
        if (!flyout)
        {
            return;
        }

        auto items = flyout.Items();
        items.Clear();

        // 1. New blank rule
        {
            Controls::MenuFlyoutItem blankItem;
            blankItem.Text(RS_(L"LinkTooltip_AddRuleMenu_NewBlankRule/Text"));

            Controls::FontIcon plusIcon;
            plusIcon.FontFamily(Media::FontFamily{ L"Segoe Fluent Icons, Segoe MDL2 Assets" });
            plusIcon.Glyph(L"\xE710");
            blankItem.Icon(plusIcon);

            blankItem.Click([this](const auto&, const auto&) {
                const auto rule = _ViewModel.RequestAddRule();
                Dispatcher().RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, [weakThis{ get_weak() }, rule]() {
                    if (const auto self{ weakThis.get() })
                    {
                        self->_ViewModel.CurrentRule(rule);
                    }
                });
            });
            items.Append(blankItem);
        }

        items.Append(Controls::MenuFlyoutSeparator{});

        for (const auto& cat : _categorizedPresets())
        {
            if (cat.presets.empty())
            {
                continue;
            }

            Controls::MenuFlyoutSubItem subMenu;
            subMenu.Text(winrt::hstring{ cat.name });

            // A category with nothing left to add is disabled at the top level, so
            // you can see there is no point opening it.
            auto anyAvailable = false;

            for (const auto* preset : cat.presets)
            {
                Controls::MenuFlyoutItem item;
                item.Text(winrt::hstring{ preset->name });
                if (!preset->description.empty())
                {
                    Controls::ToolTipService::SetToolTip(item, box_value(winrt::hstring{ preset->description }));
                }

                const winrt::hstring presetId{ preset->id };

                // Adding a preset twice produces two rules matching the same links,
                // where only the first can ever win. Disable rather than hide, so the
                // menu keeps a stable shape and can say why.
                if (_ViewModel.IsPresetInUse(presetId))
                {
                    item.IsEnabled(false);
                    Controls::ToolTipService::SetToolTip(item, box_value(RS_(L"LinkTooltip_AddRuleMenu_AlreadyAdded")));
                    subMenu.Items().Append(item);
                    continue;
                }
                anyAvailable = true;

                item.Click([this, presetId](const auto&, const auto&) {
                    const auto rule = _ViewModel.RequestAddRuleWithPreset(presetId);
                    Dispatcher().RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, [weakThis{ get_weak() }, rule]() {
                        if (const auto self{ weakThis.get() })
                        {
                            self->_ViewModel.CurrentRule(rule);
                        }
                    });
                });
                subMenu.Items().Append(item);
            }
            subMenu.IsEnabled(anyAvailable);
            items.Append(subMenu);
        }
    }

    // Same menu as Add rule, and for the same reason: filling this rule from a
    // preset another rule already is produces two rules matching the same links,
    // where only the first can ever win. The one difference is which rules count --
    // re-applying the preset THIS rule already is re-syncs it, so that entry stays
    // live rather than greying itself out.
    void LinkTooltip::ApplyPresetFlyout_Opening(const IInspectable& sender, const IInspectable& /*args*/)
    {
        const auto flyout = sender.try_as<Controls::MenuFlyout>();
        if (!flyout)
        {
            return;
        }

        auto items = flyout.Items();
        items.Clear();

        for (const auto& cat : _categorizedPresets())
        {
            if (cat.presets.empty())
            {
                continue;
            }

            Controls::MenuFlyoutSubItem subMenu;
            subMenu.Text(winrt::hstring{ cat.name });

            auto anyAvailable = false;

            for (const auto* preset : cat.presets)
            {
                Controls::MenuFlyoutItem item;
                item.Text(winrt::hstring{ preset->name });
                if (!preset->description.empty())
                {
                    Controls::ToolTipService::SetToolTip(item, box_value(winrt::hstring{ preset->description }));
                }

                const winrt::hstring presetId{ preset->id };

                if (_ViewModel.IsPresetInUseElsewhere(presetId))
                {
                    item.IsEnabled(false);
                    Controls::ToolTipService::SetToolTip(item, box_value(RS_(L"LinkTooltip_AddRuleMenu_AlreadyAdded")));
                    subMenu.Items().Append(item);
                    continue;
                }
                anyAvailable = true;

                item.Click([this, presetId](const auto&, const auto&) {
                    Dispatcher().RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, [weakThis{ get_weak() }, presetId]() {
                        if (const auto self{ weakThis.get() })
                        {
                            if (const auto currentRule = self->_ViewModel.CurrentRule())
                            {
                                currentRule.ApplyPreset(presetId);
                            }
                        }
                    });
                });
                subMenu.Items().Append(item);
            }
            subMenu.IsEnabled(anyAvailable);
            items.Append(subMenu);
        }
    }

    void LinkTooltip::DeleteRule_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        const auto rule = sender.as<FrameworkElement>().Tag().as<Editor::HyperlinkTooltipRuleViewModel>();
        _ViewModel.RequestDeleteRule(rule);
    }

    // The whole row is the affordance -- the card itself is the click target, so
    // the rule comes from its Tag rather than from a button's data context.
    void LinkTooltip::EditRule_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        const auto rule = sender.as<FrameworkElement>().Tag().as<Editor::HyperlinkTooltipRuleViewModel>();
        _ViewModel.CurrentRule(rule);
    }

    void LinkTooltip::CloseRuleEditor_Click(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        _ViewModel.CurrentRule(nullptr);
    }

    void LinkTooltip::AddCustomActionButton_Click(const IInspectable& /*sender*/, const RoutedEventArgs& /*e*/)
    {
        if (const auto rule = _ViewModel.CurrentRule())
        {
            rule.RequestAddCustomAction();
        }
    }

    void LinkTooltip::DeleteCustomAction_Click(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        if (const auto rule = _ViewModel.CurrentRule())
        {
            const auto action = sender.as<FrameworkElement>().Tag().as<Editor::HyperlinkTooltipActionViewModel>();
            rule.RequestDeleteCustomAction(action);
        }
    }

    // IconPicker's file-browse mode needs an HWND to parent its dialog. It's only reachable
    // through the page (see NewTabMenu's identical WindowRoot handoff), and each custom
    // action's picker lives inside a per-row DataTemplate rather than the page's own data
    // context, so it's wired up here on Loaded instead of through x:Bind.
    void LinkTooltip::CustomActionIconPicker_Loaded(const IInspectable& sender, const RoutedEventArgs& /*e*/)
    {
        if (const auto picker = sender.try_as<Editor::IconPicker>())
        {
            picker.WindowRoot(WindowRoot());
        }
    }
}
