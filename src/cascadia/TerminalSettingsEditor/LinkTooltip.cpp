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

    // Which menu section a preset appears under.
    //
    // The integration a preset names is the answer wherever it has one -- it is
    // the thing the preset exists to configure, and it cannot fall out of step
    // with the id the way a prefix test does. Only the three subjects no
    // integration covers are routed by id, and "file" is not one of them:
    // `media-preview` and `source-code-files` both start with neither "file"
    // nor "git", so the old prefix test filed both under General, which is why
    // "Files & Media" was an empty heading that never drew.
    static std::vector<PresetCategory> _categorizedPresets()
    {
        std::vector<PresetCategory> categories;
        for (const auto& preset : GetLinkTooltipPresets())
        {
            std::wstring_view category = L"General";
            if (!preset.integration.empty()) category = preset.name.substr(0, preset.name.find(L':'));
            else if (preset.fileTypeGroup != Model::HyperlinkFileTypeGroup::None || !preset.customExtensions.empty()) category = L"Files & Media";
            else if (preset.id.starts_with(L"git")) category = L"Git";
            auto found = std::find_if(categories.begin(), categories.end(), [&](const auto& entry) { return entry.name == category; });
            if (found == categories.end()) { categories.push_back({ std::wstring{ category }, {} }); found = categories.end() - 1; }
            found->presets.push_back(&preset);
        }
        return categories;
    }

    void LinkTooltip::DuplicateRule_Click(const IInspectable&, const RoutedEventArgs&)
    {
        _ViewModel.RequestDuplicateRule(_ViewModel.CurrentRule());
    }

    void LinkTooltip::AddRuleFlyout_Opening(const IInspectable& sender, const IInspectable&) { _buildPresetPicker(sender, false); }
    void LinkTooltip::ApplyPresetFlyout_Opening(const IInspectable& sender, const IInspectable&) { _buildPresetPicker(sender, true); }

    void LinkTooltip::_buildPresetPicker(const IInspectable& sender, bool applying)
    {
        const auto flyout = sender.as<Controls::Flyout>();
        Controls::StackPanel root;
        root.Width(420);
        root.Spacing(8);
        Controls::TextBox search;
        search.PlaceholderText(L"Search presets");
        Automation::AutomationProperties::SetName(search, L"Search presets");
        root.Children().Append(search);
        Controls::ScrollViewer scroll;
        scroll.MaxHeight(460);
        Controls::StackPanel results;
        results.Spacing(4);
        scroll.Content(results);
        root.Children().Append(scroll);
        flyout.Content(root);
        const auto populate = [weak = get_weak(), host = make_weak(results), input = make_weak(search), popup = make_weak(flyout), applying] {
            const auto self = weak.get();
            const auto list = host.get();
            const auto queryBox = input.get();
            if (!self || !list || !queryBox) return;
            list.Children().Clear();
            auto query = std::wstring{ queryBox.Text() };
            std::transform(query.begin(), query.end(), query.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            size_t matches = 0;
            for (const auto& category : _categorizedPresets())
            {
                bool headingAdded = false;
                for (const auto* preset : category.presets)
                {
                    auto haystack = std::wstring{ preset->name } + L" " + std::wstring{ preset->description } + L" " + std::wstring{ preset->integration } + L" " + std::wstring{ preset->id };
                    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
                    if (!query.empty() && haystack.find(query) == haystack.npos) continue;
                    if (!headingAdded)
                    {
                        Controls::TextBlock heading;
                        heading.Text(category.name);
                        heading.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
                        heading.Margin(Thickness{ 0, 8, 0, 2 });
                        list.Children().Append(heading);
                        headingAdded = true;
                    }
                    ++matches;
                    const hstring id{ preset->id };
                    Controls::Button button;
                    button.HorizontalAlignment(HorizontalAlignment::Stretch);
                    button.HorizontalContentAlignment(HorizontalAlignment::Left);
                    Controls::StackPanel label;
                    Controls::TextBlock name;
                    name.Text(preset->name);
                    name.TextWrapping(TextWrapping::Wrap);
                    label.Children().Append(name);
                    Controls::TextBlock description;
                    description.Text(preset->description);
                    description.TextWrapping(TextWrapping::Wrap);
                    description.FontSize(12);
                    description.Opacity(0.7);
                    label.Children().Append(description);
                    button.Content(label);
                    button.IsEnabled(!(applying ? self->_ViewModel.IsPresetInUseElsewhere(id) : self->_ViewModel.IsPresetInUse(id)));
                    button.Click([weak, popup, applying, id](auto&&, auto&&) {
                        if (const auto current = weak.get())
                        {
                            if (const auto menu = popup.get()) menu.Hide();
                            current->Dispatcher().RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, [weak, applying, id] {
                                if (const auto target = weak.get())
                                {
                                    if (applying) { if (const auto rule = target->_ViewModel.CurrentRule()) rule.ApplyPreset(id); }
                                    else target->_ViewModel.CurrentRule(target->_ViewModel.RequestAddRuleWithPreset(id));
                                }
                            });
                        }
                    });
                    list.Children().Append(button);
                }
            }
            if (!matches) { Controls::TextBlock empty; empty.Text(L"No presets match your search."); list.Children().Append(empty); }
        };
        search.TextChanged([populate](auto&&, auto&&) { populate(); });
        populate();
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
