// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "LinkPreviewPaneContent.h"
#include "LinkPreviewPaneContent.g.cpp"

using namespace winrt::Microsoft::Terminal;
using namespace winrt::Microsoft::Terminal::Settings;
using namespace winrt::Microsoft::Terminal::Settings::Model;

// Deliberately NOT "using namespace winrt::Windows::UI::Xaml::Controls": that
// would bring the class Controls::Control into scope alongside the namespace
// Microsoft::Terminal::Control, and every Control::HyperlinkPreview below would
// stop compiling as ambiguous.
using namespace winrt::Windows::UI::Xaml;

namespace winrt
{
    namespace MUX = Microsoft::UI::Xaml;
    namespace WUX = Windows::UI::Xaml;
    using IInspectable = Windows::Foundation::IInspectable;
}

namespace winrt::TerminalApp::implementation
{
    LinkPreviewPaneContent::LinkPreviewPaneContent()
    {
        InitializeComponent();
    }

    void LinkPreviewPaneContent::SetPreviewProvider(const Control::IHyperlinkPreviewProvider& provider)
    {
        _provider = provider;
    }

    // Safe to call on a pane that is already open, because that is the whole point
    // of reusing one: pressing "Show in pane" on a second link retargets the pane
    // that is already there rather than splitting again.
    void LinkPreviewPaneContent::ShowLink(const winrt::hstring& text, const winrt::hstring& integrationHint)
    {
        if (text.empty())
        {
            return;
        }

        if (_preview && text == _sourceText && integrationHint == _integrationHint)
        {
            // Already showing exactly this. Re-fetching would only make the pane
            // flicker through its loading state for no new information.
            return;
        }

        _sourceText = text;
        _integrationHint = integrationHint;
        _undoChoiceId = {};

        // Something to call the pane by until the integration says what this is.
        _title = text;
        TitleChanged.raise(*this, nullptr);

        _fetch(++_generation, false);
    }

    safe_void_coroutine LinkPreviewPaneContent::_fetch(uint32_t generation, bool refresh)
    {
        const auto weakThis{ get_weak() };
        const auto provider{ _provider };
        const auto dispatcher{ Dispatcher() };
        const auto text{ _sourceText };
        const auto hint{ _integrationHint };
        if (!provider || !dispatcher || text.empty())
        {
            co_return;
        }

        _setLoading(true);

        Control::HyperlinkPreview preview{ nullptr };
        try
        {
            preview = refresh ? co_await provider.RefreshAsync(text, hint) :
                                co_await provider.GetPreviewAsync(text, hint);
        }
        CATCH_LOG();

        co_await wil::resume_foreground(dispatcher);

        const auto self = weakThis.get();
        if (!self || self->_generation != generation)
        {
            co_return;
        }

        self->_setLoading(false);
        self->_render(preview);
    }

    void LinkPreviewPaneContent::_setLoading(bool loading)
    {
        LoadingRing().IsActive(loading);
        LoadingRing().Visibility(loading ? Visibility::Visible : Visibility::Collapsed);
        RefreshButton().IsEnabled(!loading);
    }

    void LinkPreviewPaneContent::_setError(const winrt::hstring& message)
    {
        ErrorText().Text(message);
        ErrorText().Visibility(message.empty() ? Visibility::Collapsed : Visibility::Visible);
    }

    void LinkPreviewPaneContent::_render(const Control::HyperlinkPreview& preview)
    {
        _preview = preview;

        if (!preview)
        {
            HeaderIcon().Content(nullptr);
            IntegrationName().Text(winrt::hstring{});
            FieldsHost().Children().Clear();
            BodyHost().Children().Clear();
            CommentsHost().Children().Clear();
            _rebuildTabStrip(nullptr);
            _rebuildActions(nullptr);
            _setError(RS_(L"LinkPreviewUnavailable"));
            return;
        }

        IntegrationName().Text(preview.IntegrationName());
        if (const auto icon = preview.IntegrationIcon(); !icon.empty())
        {
            HeaderIcon().Content(winrt::Microsoft::Terminal::UI::IconPathConverter::IconWUX(icon));
        }
        else
        {
            HeaderIcon().Content(nullptr);
        }

        _setError(preview.Error());

        // The title is the pane's title too, so the tab header follows whatever the
        // integration decided this thing is called.
        auto title = _sourceText;
        if (const auto fields = preview.Fields())
        {
            for (const auto& field : fields)
            {
                if (field && field.IsTitle() && !field.Value().empty())
                {
                    title = field.Value();
                    break;
                }
            }
        }
        TitleText().Text(title);
        if (title != _title)
        {
            _title = title;
            TitleChanged.raise(*this, nullptr);
        }

        _renderFields(preview);
        _rebuildTabStrip(preview);
        _rebuildActions(preview);
    }

    // The pane has room for headings, so grouped fields get them. Groups appear in
    // the order the integration first mentioned them, and every field carrying the
    // same group key lands under one heading however it was interleaved.
    void LinkPreviewPaneContent::_renderFields(const Control::HyperlinkPreview& preview)
    {
        const auto host = FieldsHost();
        host.Children().Clear();

        const auto fields = preview ? preview.Fields() : nullptr;
        if (!fields)
        {
            return;
        }

        std::vector<winrt::hstring> order;
        std::vector<winrt::hstring> labels;
        std::vector<std::vector<Control::HyperlinkPreviewField>> groups;

        for (const auto& field : fields)
        {
            if (!field)
            {
                continue;
            }

            const auto key = field.Group();
            const auto found = std::find(order.begin(), order.end(), key);
            auto index = static_cast<size_t>(found - order.begin());
            if (found == order.end())
            {
                index = order.size();
                order.emplace_back(key);
                labels.emplace_back(field.GroupLabel());
                groups.emplace_back();
            }
            groups[index].emplace_back(field);
        }

        for (size_t i = 0; i < order.size(); ++i)
        {
            if (!order[i].empty())
            {
                Controls::TextBlock heading;
                heading.Text(labels[i].empty() ? order[i] : labels[i]);
                heading.FontWeight(Windows::UI::Text::FontWeight{ 600 });
                heading.Opacity(0.8);
                heading.TextWrapping(TextWrapping::Wrap);
                host.Children().Append(heading);
            }

            host.Children().Append(_makeFieldRows(groups[i]));
        }
    }

    // One Grid for the whole group, so every label in it lines up: the same reason
    // the card builds its list in code rather than from a per-row DataTemplate.
    UIElement LinkPreviewPaneContent::_makeFieldRows(const std::vector<Control::HyperlinkPreviewField>& fields)
    {
        Controls::Grid grid;
        grid.ColumnSpacing(10);
        grid.RowSpacing(3);
        {
            Controls::ColumnDefinition labelColumn;
            labelColumn.Width(GridLength{ 0, GridUnitType::Auto });
            labelColumn.MinWidth(80);
            grid.ColumnDefinitions().Append(labelColumn);

            Controls::ColumnDefinition valueColumn;
            valueColumn.Width(GridLength{ 1, GridUnitType::Star });
            grid.ColumnDefinitions().Append(valueColumn);
        }

        auto row = 0;
        for (const auto& field : fields)
        {
            // The title already has its own place at the top of the pane, so it is
            // skipped here rather than repeated inside the field list.
            if (field.IsTitle())
            {
                continue;
            }

            Controls::RowDefinition rowDefinition;
            rowDefinition.Height(GridLength{ 0, GridUnitType::Auto });
            grid.RowDefinitions().Append(rowDefinition);

            Controls::TextBlock label;
            label.Text(field.Label());
            label.Opacity(0.7);
            label.VerticalAlignment(VerticalAlignment::Top);
            label.TextWrapping(TextWrapping::Wrap);
            Controls::Grid::SetRow(label, row);
            Controls::Grid::SetColumn(label, 0);
            grid.Children().Append(label);

            // A Grid and not a horizontal StackPanel: a horizontal StackPanel
            // measures with infinite width, so nothing inside it ever wraps.
            Controls::Grid cell;
            cell.ColumnSpacing(6);
            {
                Controls::ColumnDefinition iconColumn;
                iconColumn.Width(GridLength{ 0, GridUnitType::Auto });
                cell.ColumnDefinitions().Append(iconColumn);

                Controls::ColumnDefinition valueColumn;
                valueColumn.Width(GridLength{ 1, GridUnitType::Star });
                cell.ColumnDefinitions().Append(valueColumn);
            }

            if (field.HasIcon())
            {
                Controls::Image icon;
                icon.Width(16);
                icon.Height(16);
                icon.VerticalAlignment(VerticalAlignment::Top);
                icon.Margin(Thickness{ 0, 2, 0, 0 });
                icon.Source(Control::HyperlinkPreviewHelpers::ImageFromUri(field.IconUri()));
                Controls::Grid::SetColumn(icon, 0);
                cell.Children().Append(icon);
            }

            if (field.IsBadge())
            {
                Controls::Border badge;
                badge.Padding(Thickness{ 6, 1, 6, 1 });
                badge.CornerRadius(Windows::UI::Xaml::CornerRadius{ 4, 4, 4, 4 });
                badge.Background(Control::HyperlinkPreviewHelpers::BadgeBrush(field.Color()));
                badge.HorizontalAlignment(HorizontalAlignment::Left);
                badge.VerticalAlignment(VerticalAlignment::Top);

                Controls::TextBlock badgeText;
                badgeText.FontSize(12);
                badgeText.Text(field.Value());
                badge.Child(badgeText);

                Controls::Grid::SetColumn(badge, 1);
                cell.Children().Append(badge);
            }
            else
            {
                Controls::TextBlock value;
                value.Text(field.Value());
                value.TextWrapping(TextWrapping::Wrap);
                value.IsTextSelectionEnabled(true);
                Controls::Grid::SetColumn(value, 1);
                cell.Children().Append(value);
            }

            Controls::Grid::SetRow(cell, row);
            Controls::Grid::SetColumn(cell, 1);
            grid.Children().Append(cell);
            ++row;
        }

        return grid;
    }

    // Unlike the card, the pane has room to render a markdown body as markdown.
    // Format is only ever "text" or "markdown": an ADF document is flattened to text
    // in the service and reported as "text", so there is no third branch to write.
    UIElement LinkPreviewPaneContent::_makeBodyElement(const winrt::hstring& body, const winrt::hstring& format)
    {
        if (til::equals_insensitive_ascii(std::wstring_view{ format }, L"markdown") && !body.empty())
        {
            try
            {
                const auto baseUrl = _preview ? _preview.ResolvedUri() : winrt::hstring{};
                return winrt::Microsoft::Terminal::UI::Markdown::Builder::Convert(body, baseUrl);
            }
            CATCH_LOG();
        }

        Controls::TextBlock text;
        text.Text(body);
        text.TextWrapping(TextWrapping::Wrap);
        text.IsTextSelectionEnabled(true);
        return text;
    }

    void LinkPreviewPaneContent::_renderBody(const Control::HyperlinkPreviewTab& tab)
    {
        const auto host = BodyHost();
        host.Children().Clear();

        if (!tab)
        {
            return;
        }

        host.Children().Append(_makeBodyElement(tab.Body(), tab.Format()));
    }

    void LinkPreviewPaneContent::_renderComments(const Control::HyperlinkPreviewTab& tab)
    {
        const auto host = CommentsHost();
        host.Children().Clear();

        const auto comments = tab ? tab.Comments() : nullptr;
        if (!comments)
        {
            return;
        }

        for (const auto& comment : comments)
        {
            if (!comment)
            {
                continue;
            }

            Controls::Grid entry;
            entry.ColumnSpacing(8);
            {
                Controls::ColumnDefinition avatarColumn;
                avatarColumn.Width(GridLength{ 0, GridUnitType::Auto });
                entry.ColumnDefinitions().Append(avatarColumn);

                Controls::ColumnDefinition bodyColumn;
                bodyColumn.Width(GridLength{ 1, GridUnitType::Star });
                entry.ColumnDefinitions().Append(bodyColumn);
            }

            if (const auto avatar = comment.AvatarUri(); !avatar.empty())
            {
                Controls::Image image;
                image.Width(24);
                image.Height(24);
                image.VerticalAlignment(VerticalAlignment::Top);
                image.Source(Control::HyperlinkPreviewHelpers::ImageFromUri(avatar));
                Controls::Grid::SetColumn(image, 0);
                entry.Children().Append(image);
            }

            Controls::StackPanel text;
            text.Spacing(2);

            Controls::TextBlock heading;
            auto headingText = std::wstring{ comment.Author() };
            if (const auto time = comment.Time(); !time.empty())
            {
                if (!headingText.empty())
                {
                    headingText.append(L" · ");
                }
                headingText.append(std::wstring_view{ time });
            }
            heading.Text(winrt::hstring{ headingText });
            heading.Opacity(0.7);
            heading.FontSize(12);
            heading.TextWrapping(TextWrapping::Wrap);
            text.Children().Append(heading);

            // A comment body is formatted the same way its tab says, exactly like a
            // Body tab: GitHub's arrive as markdown and Jira's as flattened text, and
            // nothing on the comment itself tells the two apart.
            text.Children().Append(_makeBodyElement(comment.Body(), tab.Format()));

            Controls::Grid::SetColumn(text, 1);
            entry.Children().Append(text);

            host.Children().Append(entry);
        }
    }

    void LinkPreviewPaneContent::_rebuildTabStrip(const Control::HyperlinkPreview& preview)
    {
        const auto strip = TabStrip();
        strip.Children().Clear();
        _selectedTab = -1;

        const auto tabs = preview ? preview.Tabs() : nullptr;
        if (!tabs || tabs.Size() == 0)
        {
            strip.Visibility(Visibility::Collapsed);
            _showTab(-1);
            return;
        }

        auto addButton = [&](const winrt::hstring& label, int32_t index) {
            Controls::Primitives::ToggleButton button;
            button.Content(winrt::box_value(label));
            button.Tag(winrt::box_value(index));
            button.Padding(Thickness{ 10, 3, 10, 3 });
            button.MinWidth(0);
            button.IsChecked(index == _selectedTab);
            button.Click({ this, &LinkPreviewPaneContent::_tabClick });
            strip.Children().Append(button);
        };

        addButton(RS_(L"LinkPreviewFieldsTabLabel"), -1);
        for (uint32_t i = 0; i < tabs.Size(); ++i)
        {
            const auto tab = tabs.GetAt(i);
            addButton(tab ? tab.Label() : winrt::hstring{}, gsl::narrow_cast<int32_t>(i));
        }

        strip.Visibility(Visibility::Visible);
        _showTab(-1);
    }

    void LinkPreviewPaneContent::_showTab(int32_t index)
    {
        Control::HyperlinkPreviewTab tab{ nullptr };
        if (index >= 0 && _preview)
        {
            if (const auto tabs = _preview.Tabs(); tabs && gsl::narrow_cast<uint32_t>(index) < tabs.Size())
            {
                tab = tabs.GetAt(gsl::narrow_cast<uint32_t>(index));
            }
        }
        if (!tab)
        {
            index = -1;
        }

        _selectedTab = index;

        const auto kind = tab ? tab.Kind() : Control::HyperlinkPreviewTabKind::Fields;
        const auto showFields = !tab || kind == Control::HyperlinkPreviewTabKind::Fields;
        const auto showBody = tab && kind == Control::HyperlinkPreviewTabKind::Body;
        const auto showComments = tab && kind == Control::HyperlinkPreviewTabKind::Comments;

        if (showBody)
        {
            _renderBody(tab);
        }
        if (showComments)
        {
            _renderComments(tab);
        }

        FieldsHost().Visibility(showFields ? Visibility::Visible : Visibility::Collapsed);
        BodyHost().Visibility(showBody ? Visibility::Visible : Visibility::Collapsed);
        CommentsHost().Visibility(showComments ? Visibility::Visible : Visibility::Collapsed);

        // ToggleButton has no radio behaviour of its own, so it is enforced here.
        for (const auto& child : TabStrip().Children())
        {
            if (const auto button = child.try_as<Controls::Primitives::ToggleButton>())
            {
                const auto buttonIndex = button.Tag().try_as<int32_t>();
                button.IsChecked(buttonIndex && *buttonIndex == index);
            }
        }
    }

    void LinkPreviewPaneContent::_tabClick(const IInspectable& sender, const RoutedEventArgs&)
    {
        if (const auto button = sender.try_as<Controls::Primitives::ToggleButton>())
        {
            if (const auto index = button.Tag().try_as<int32_t>())
            {
                _showTab(*index);
            }
        }
    }

    // Only the first action is offered, matching the card: no integration declares
    // more than one today, and a second would need a layout of its own.
    void LinkPreviewPaneContent::_rebuildActions(const Control::HyperlinkPreview& preview)
    {
        const auto combo = ActionOptions();
        combo.Items().Clear();
        ActionFields().Children().Clear();
        ActionFields().Visibility(Visibility::Collapsed);
        ActionError().Visibility(Visibility::Collapsed);
        _action = nullptr;

        const auto actions = preview ? preview.Actions() : nullptr;
        if (!actions || actions.Size() == 0)
        {
            ActionRow().Visibility(Visibility::Collapsed);
            return;
        }

        _action = actions.GetAt(0);
        const auto options = _action ? _action.Options() : nullptr;
        if (!options || options.Size() == 0)
        {
            _action = nullptr;
            ActionRow().Visibility(Visibility::Collapsed);
            return;
        }

        for (const auto& option : options)
        {
            if (!option)
            {
                continue;
            }

            Controls::StackPanel content;
            content.Orientation(Controls::Orientation::Horizontal);
            content.Spacing(6);

            Controls::TextBlock label;
            label.Text(option.Label());
            content.Children().Append(label);

            if (const auto badgeText = option.Badge(); !badgeText.empty())
            {
                Controls::TextBlock arrow;
                arrow.Text(winrt::hstring{ L"→" });
                arrow.Opacity(0.7);
                content.Children().Append(arrow);

                Controls::Border badge;
                badge.Padding(Thickness{ 6, 1, 6, 1 });
                badge.CornerRadius(Windows::UI::Xaml::CornerRadius{ 4, 4, 4, 4 });
                badge.Background(Control::HyperlinkPreviewHelpers::BadgeBrush(option.Color()));
                badge.VerticalAlignment(VerticalAlignment::Center);

                Controls::TextBlock badgeLabel;
                badgeLabel.FontSize(12);
                badgeLabel.Text(badgeText);
                badge.Child(badgeLabel);
                content.Children().Append(badge);
            }

            Controls::ComboBoxItem item;
            item.Content(content);
            item.Tag(option);
            combo.Items().Append(item);
        }

        ActionRow().Visibility(Visibility::Visible);
        UndoButton().Visibility(_undoChoiceId.empty() ? Visibility::Collapsed : Visibility::Visible);
        _updateApplyState();
    }

    Control::HyperlinkPreviewActionOption LinkPreviewPaneContent::_selectedOption()
    {
        if (const auto item = ActionOptions().SelectedItem().try_as<Controls::ComboBoxItem>())
        {
            return item.Tag().try_as<Control::HyperlinkPreviewActionOption>();
        }
        return nullptr;
    }

    void LinkPreviewPaneContent::_updateActionFields()
    {
        const auto host = ActionFields();
        host.Children().Clear();

        const auto option = _selectedOption();
        const auto fields = option ? option.Fields() : nullptr;
        if (!option || !option.NeedsFields() || !fields)
        {
            host.Visibility(Visibility::Collapsed);
            _updateApplyState();
            return;
        }

        auto onChanged = [weakThis = get_weak()]() {
            if (const auto self = weakThis.get())
            {
                self->_updateApplyState();
            }
        };

        for (const auto& field : fields)
        {
            if (!field)
            {
                continue;
            }

            Controls::TextBlock label;
            label.Text(field.Label());
            label.Opacity(0.7);
            label.TextWrapping(TextWrapping::Wrap);
            host.Children().Append(label);

            const auto options = field.Options();
            if (options && options.Size() > 0)
            {
                Controls::ComboBox picker;
                picker.Tag(winrt::box_value(field.Key()));
                picker.MinWidth(180);
                for (const auto& choice : options)
                {
                    picker.Items().Append(winrt::box_value(choice));
                }
                picker.SelectionChanged([onChanged](auto&&, auto&&) { onChanged(); });
                host.Children().Append(picker);
            }
            else
            {
                Controls::TextBox box;
                box.Tag(winrt::box_value(field.Key()));
                box.TextChanged([onChanged](auto&&, auto&&) { onChanged(); });
                host.Children().Append(box);
            }
        }

        host.Visibility(host.Children().Size() > 0 ? Visibility::Visible : Visibility::Collapsed);
        _updateApplyState();
    }

    void LinkPreviewPaneContent::_updateApplyState()
    {
        const auto option = _selectedOption();
        auto ready = static_cast<bool>(option);

        if (ready && option.NeedsFields())
        {
            const auto values = _collectFieldValues();
            if (const auto fields = option.Fields())
            {
                for (const auto& field : fields)
                {
                    if (!field || !field.Required())
                    {
                        continue;
                    }
                    if (!values.HasKey(field.Key()) || values.Lookup(field.Key()).empty())
                    {
                        ready = false;
                        break;
                    }
                }
            }
        }

        ApplyButton().IsEnabled(ready);
    }

    winrt::Windows::Foundation::Collections::IMap<winrt::hstring, winrt::hstring> LinkPreviewPaneContent::_collectFieldValues()
    {
        auto values = winrt::single_threaded_map<winrt::hstring, winrt::hstring>();

        for (const auto& child : ActionFields().Children())
        {
            if (const auto box = child.try_as<Controls::TextBox>())
            {
                if (const auto key = box.Tag().try_as<winrt::hstring>(); key && !key->empty())
                {
                    values.Insert(*key, box.Text());
                }
            }
            else if (const auto picker = child.try_as<Controls::ComboBox>())
            {
                if (const auto key = picker.Tag().try_as<winrt::hstring>(); key && !key->empty())
                {
                    const auto selected = picker.SelectedItem().try_as<winrt::hstring>();
                    values.Insert(*key, selected ? *selected : winrt::hstring{});
                }
            }
        }

        return values;
    }

    void LinkPreviewPaneContent::_setActionBusy(bool busy)
    {
        ActionProgress().IsActive(busy);
        ActionProgress().Visibility(busy ? Visibility::Visible : Visibility::Collapsed);
        ActionOptions().IsEnabled(!busy);
        UndoButton().IsEnabled(!busy);
        // A StackPanel is not a Control, so it has no IsEnabled: block the input
        // and dim it instead, which is what IsEnabled would have looked like.
        ActionFields().IsHitTestVisible(!busy);
        ActionFields().Opacity(busy ? 0.5 : 1.0);
        if (busy)
        {
            ApplyButton().IsEnabled(false);
        }
        else
        {
            _updateApplyState();
        }
    }

    void LinkPreviewPaneContent::_optionChanged(const IInspectable&, const Controls::SelectionChangedEventArgs&)
    {
        ActionError().Visibility(Visibility::Collapsed);
        _updateActionFields();
    }

    void LinkPreviewPaneContent::_applyClick(const IInspectable&, const RoutedEventArgs&)
    {
        if (const auto option = _selectedOption())
        {
            _invokeAction(_generation, option.Id());
        }
    }

    void LinkPreviewPaneContent::_undoClick(const IInspectable&, const RoutedEventArgs&)
    {
        const auto choiceId = _undoChoiceId;
        if (choiceId.empty())
        {
            return;
        }

        // Undo is the far end's own way back, not the form the user filled in for
        // the change being undone, so nothing stale rides along with the request.
        _undoChoiceId = {};
        UndoButton().Visibility(Visibility::Collapsed);
        ActionFields().Children().Clear();
        ActionFields().Visibility(Visibility::Collapsed);

        _invokeAction(_generation, choiceId);
    }

    void LinkPreviewPaneContent::_refreshClick(const IInspectable&, const RoutedEventArgs&)
    {
        _fetch(++_generation, true);
    }

    // Two round trips (the change, then a refresh that shows it) under the same
    // generation guard the fetch uses: the pane may have been retargeted meanwhile.
    safe_void_coroutine LinkPreviewPaneContent::_invokeAction(uint32_t generation, winrt::hstring choiceId)
    {
        const auto weakThis{ get_weak() };
        const auto provider{ _provider };
        const auto dispatcher{ Dispatcher() };
        const auto action{ _action };
        const auto preview{ _preview };
        if (!provider || !dispatcher || !action || !preview)
        {
            co_return;
        }

        auto sourceText = preview.SourceText();
        if (sourceText.empty())
        {
            sourceText = _sourceText;
        }
        const auto hint = _integrationHint;
        const auto actionKey = action.Key();
        const auto fieldValues = _collectFieldValues();

        _setActionBusy(true);
        ActionError().Visibility(Visibility::Collapsed);

        Control::HyperlinkActionResult result{ nullptr };
        try
        {
            result = co_await provider.InvokeActionAsync(sourceText, hint, actionKey, choiceId, fieldValues);
        }
        CATCH_LOG();

        Control::HyperlinkPreview refreshed{ nullptr };
        if (result && result.Ok())
        {
            try
            {
                refreshed = co_await provider.RefreshAsync(sourceText, hint);
            }
            CATCH_LOG();
        }

        co_await wil::resume_foreground(dispatcher);

        const auto self = weakThis.get();
        if (!self || self->_generation != generation)
        {
            co_return;
        }

        self->_setActionBusy(false);

        if (!result || !result.Ok())
        {
            auto message = result ? result.Error() : winrt::hstring{};
            if (message.empty())
            {
                message = RS_(L"LinkPreviewActionFailed");
            }
            self->ActionError().Text(message);
            self->ActionError().Visibility(Visibility::Visible);
            co_return;
        }

        // Repaint before the undo state is written: rebuilding the action row is
        // what puts the Undo button back on screen.
        if (refreshed)
        {
            self->_render(refreshed);
        }

        self->_undoChoiceId = result.UndoChoiceId();
        const auto hasUndo = !self->_undoChoiceId.empty();
        if (hasUndo)
        {
            if (const auto undoLabel = result.UndoLabel(); !undoLabel.empty())
            {
                if (!self->_undoDefaultContent)
                {
                    self->_undoDefaultContent = self->UndoButton().Content();
                }
                self->UndoButton().Content(winrt::box_value(undoLabel));
            }
        }
        else if (self->_undoDefaultContent)
        {
            self->UndoButton().Content(self->_undoDefaultContent);
        }
        self->UndoButton().Visibility(hasUndo ? Visibility::Visible : Visibility::Collapsed);

        // An empty UndoChoiceId is a real answer: the integration went looking for a
        // way back and there isn't one. Left unsaid it would look exactly like a
        // button that has not appeared yet, so the pane says which of the two it is.
        if (!hasUndo)
        {
            self->ActionError().Text(RS_(L"LinkPreviewNoUndo"));
            self->ActionError().Visibility(Visibility::Visible);
        }
    }

    // The page does the actual silencing, being the only thing that can reach every
    // control, so this just says which way the switch went.
    void LinkPreviewPaneContent::_hideTooltipsToggled(const IInspectable&, const RoutedEventArgs&)
    {
        const auto on = HideTooltipsSwitch().IsOn();
        if (on == _hideTooltips)
        {
            return;
        }
        _hideTooltips = on;
        HideTooltipsChanged.raise(*this, nullptr);
    }

    void LinkPreviewPaneContent::_closeClick(const IInspectable&, const RoutedEventArgs&)
    {
        Close();
    }

#pragma region IPaneContent

    FrameworkElement LinkPreviewPaneContent::GetRoot()
    {
        return *this;
    }

    void LinkPreviewPaneContent::Focus(FocusState reason)
    {
        CloseButton().Focus(reason);
    }

    void LinkPreviewPaneContent::Close()
    {
        // Whoever closed the pane also meant to stop silencing the terminals, so the
        // switch reports itself off on the way out rather than leaving every control
        // mute with nothing on screen to explain why.
        if (_hideTooltips)
        {
            _hideTooltips = false;
            HideTooltipsChanged.raise(*this, nullptr);
        }
        CloseRequested.raise(*this, nullptr);
    }

    INewContentArgs LinkPreviewPaneContent::GetNewTerminalArgs(BuildStartupKind /*kind*/) const
    {
        return BaseContentArgs(L"x-link-preview");
    }

    winrt::hstring LinkPreviewPaneContent::Icon() const
    {
        static constexpr std::wstring_view glyph{ L"\xE71B" }; // Link
        return winrt::hstring{ glyph };
    }

#pragma endregion
}
