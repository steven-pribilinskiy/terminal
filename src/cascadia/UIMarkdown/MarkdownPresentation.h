// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
#pragma once
#include "SyntaxHighlight.h"
#include "Frontmatter.h"
#include "MarkdownToXaml.h"
#include <winrt/Windows.UI.ViewManagement.h>

namespace MarkdownPresentation
{
    namespace Xaml = winrt::Windows::UI::Xaml;
    namespace Controls = Xaml::Controls;

    inline Controls::TextBlock Code(const winrt::hstring& source, const winrt::hstring& language)
    {
        Controls::TextBlock block;
        block.FontFamily(Xaml::Media::FontFamily{ L"Cascadia Mono, Consolas" });
        block.IsTextSelectionEnabled(true);
        block.ContextFlyout(winrt::Microsoft::Terminal::UI::TextMenuFlyout{});
        const auto paint = [weak = winrt::make_weak(block), source, language] {
            const auto text = weak.get();
            if (!text) return;
            text.Inlines().Clear();
            const bool dark = text.ActualTheme() == Xaml::ElementTheme::Dark;
            const bool contrast = winrt::Windows::UI::ViewManagement::AccessibilitySettings{}.HighContrast();
            for (const auto& token : MarkdownPreview::Highlight(std::wstring_view{ source }, std::wstring_view{ language }))
            {
                Xaml::Documents::Run run;
                run.Text(std::wstring_view{ source }.substr(token.start, token.length));
                if (!contrast && token.kind != MarkdownPreview::TokenKind::Plain)
                {
                    uint32_t rgb = 0;
                    switch (token.kind)
                    {
                    case MarkdownPreview::TokenKind::Keyword: rgb = dark ? 0xC586C0 : 0x7B1FA2; break;
                    case MarkdownPreview::TokenKind::String: rgb = dark ? 0xCE9178 : 0xA31515; break;
                    case MarkdownPreview::TokenKind::Number: rgb = dark ? 0xB5CEA8 : 0x096A46; break;
                    case MarkdownPreview::TokenKind::Comment: rgb = dark ? 0x8CBF78 : 0x38732B; break;
                    case MarkdownPreview::TokenKind::Key: rgb = dark ? 0x9CDCFE : 0x005A9E; break;
                    default: rgb = dark ? 0x569CD6 : 0x0451A5; break;
                    }
                    run.Foreground(Xaml::Media::SolidColorBrush{ winrt::Windows::UI::Color{ 255, static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb) } });
                }
                text.Inlines().Append(run);
            }
        };
        block.Loaded([paint](auto&&, auto&&) { paint(); });
        block.ActualThemeChanged([paint](auto&&, auto&&) { paint(); });
        paint();
        return block;
    }

    inline Xaml::FrameworkElement Preview(const winrt::hstring& source, const winrt::hstring& baseUrl, bool raw)
    {
        if (raw) return Code(source, L"markdown");
        const auto utf8 = winrt::to_string(source);
        const auto metadata = MarkdownPreview::ParseFrontmatter(utf8);
        Controls::StackPanel root;
        root.Spacing(12);
        if (metadata.present)
        {
            Controls::StackPanel fields;
            fields.Spacing(6);
            Controls::TextBlock title;
            title.Text(L"Metadata");
            title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            fields.Children().Append(title);
            if (!metadata.error.empty())
            {
                Controls::TextBlock error;
                error.Text(winrt::to_hstring(metadata.error));
                error.TextWrapping(Xaml::TextWrapping::Wrap);
                fields.Children().Append(error);
                fields.Children().Append(Code(winrt::to_hstring(metadata.yaml.substr(0, 65536)), L"yaml"));
            }
            else for (const auto& row : metadata.rows)
            {
                Controls::TextBlock field;
                field.IsTextSelectionEnabled(true);
                field.TextWrapping(Xaml::TextWrapping::Wrap);
                field.Margin(Xaml::Thickness{ static_cast<double>(row.depth > 0 ? row.depth - 1 : 0) * 14, 0, 0, 0 });
                if (!row.key.empty())
                {
                    Xaml::Documents::Run key;
                    key.Text(winrt::to_hstring(row.key + (row.value.empty() ? "" : ": ")));
                    key.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                    field.Inlines().Append(key);
                }
                Xaml::Documents::Run value;
                value.Text(winrt::to_hstring(row.value));
                field.Inlines().Append(value);
                fields.Children().Append(field);
            }
            Controls::Border card;
            card.Padding(Xaml::Thickness{ 12, 10, 12, 10 });
            card.BorderThickness(Xaml::Thickness{ 1, 1, 1, 1 });
            card.CornerRadius(Xaml::CornerRadius{ 4, 4, 4, 4 });
            card.BorderBrush(Xaml::Media::SolidColorBrush{ winrt::Windows::UI::Color{ 100, 128, 128, 128 } });
            card.Child(fields);
            root.Children().Append(card);
        }
        root.Children().Append(MarkdownToXaml::Convert(metadata.body, baseUrl));
        return root;
    }
}
