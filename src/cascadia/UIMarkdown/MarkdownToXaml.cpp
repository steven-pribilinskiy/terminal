// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "CodeBlock.h"
#include "MarkdownToXaml.h"
#include "MarkdownBlocks.h"
#include <winrt/Windows.UI.Xaml.Automation.h>

#include <cmark.h>

namespace winrt
{
    namespace MUX = Microsoft::UI::Xaml;
    namespace WUX = winrt::Windows::UI::Xaml;
    using IInspectable = Windows::Foundation::IInspectable;
}
using namespace winrt;

// Bullet points used for unordered lists.
static constexpr std::wstring_view bullets[]{
    L"• ",
    L"◦ ",
    L"▪ " // After this level, we'll keep using this one.
};
static constexpr int WidthOfBulletPoint{ 9 };
static constexpr int IndentWidth{ 3 * WidthOfBulletPoint };
static constexpr int H1FontSize{ 36 };
static constexpr int HeaderMinFontSize{ 16 };

static constexpr std::wstring_view CodeFontFamily{ L"Cascadia Mono, Consolas" };

template<typename T>
static std::string_view textFromCmarkString(const T& s) noexcept
{
    return std::string_view{ (char*)s.data, (size_t)s.len };
}
static std::string_view textFromLiteral(cmark_node* node) noexcept
{
    return cmark_node_get_literal(node);
}
static std::string_view textFromUrl(cmark_node* node) noexcept
{
    return cmark_node_get_url(node);
}

typedef wil::unique_any<cmark_node*, decltype(&cmark_node_free), cmark_node_free> unique_node;
typedef wil::unique_any<cmark_iter*, decltype(&cmark_iter_free), cmark_iter_free> unique_iter;

// Function Description:
// - Entrypoint to convert a string of markdown into a XAML RichTextBlock.
// Arguments:
// - markdownText: the markdown content to render
// - baseUrl: the current URI of the content. This will allow for relative links
//   to be appropriately resolved.
// Return Value:
// - a RichTextBlock with the rendered markdown in it.
WUX::Controls::RichTextBlock MarkdownToXaml::Convert(std::string_view markdownText, const winrt::hstring& baseUrl, size_t depth)
{
    MarkdownToXaml data{ baseUrl };
    if (depth > 8)
    {
        data._NewRun().Text(winrt::to_hstring(markdownText));
        return data._root;
    }
    for (const auto& block : MarkdownPreview::ParseRichBlocks(markdownText))
    {
        if (block.kind == MarkdownPreview::RichBlock::Kind::Table)
        {
            WUX::Controls::Grid table;
            const auto columns = std::min<size_t>(32, block.rows.front().size());
            for (size_t col = 0; col < columns; ++col)
            {
                WUX::Controls::ColumnDefinition column;
                column.Width(WUX::GridLength{ 1, WUX::GridUnitType::Star });
                table.ColumnDefinitions().Append(column);
            }
            for (size_t row = 0; row < block.rows.size(); ++row)
            {
                WUX::Controls::RowDefinition definition;
                definition.Height(WUX::GridLength{ 0, WUX::GridUnitType::Auto });
                table.RowDefinitions().Append(definition);
                for (size_t col = 0; col < columns; ++col)
                {
                    auto text = col < block.rows[row].size() ? block.rows[row][col] : std::string{};
                    for (size_t pos = 0; (pos = text.find("<br>", pos)) != text.npos; pos += 3) text.replace(pos, 4, "  \n");
                    auto body = Convert(text, baseUrl, depth + 1);
                    body.TextWrapping(WUX::TextWrapping::Wrap);
                    if (!row) body.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                    WUX::Controls::Border cell;
                    cell.Padding(WUX::Thickness{ 8, 7, 8, 7 });
                    cell.BorderThickness(WUX::Thickness{ 0.5, 0.5, 0.5, 0.5 });
                    cell.BorderBrush(WUX::Media::SolidColorBrush{ winrt::Windows::UI::Color{ 70, 128, 128, 128 } });
                    if (!row) cell.Background(WUX::Media::SolidColorBrush{ winrt::Windows::UI::Color{ 35, 128, 128, 128 } });
                    cell.Child(body);
                    WUX::Controls::Grid::SetRow(cell, static_cast<int32_t>(row));
                    WUX::Controls::Grid::SetColumn(cell, static_cast<int32_t>(col));
                    table.Children().Append(cell);
                }
            }
            data._AppendBlock(table);
        }
        else if (block.kind == MarkdownPreview::RichBlock::Kind::Callout)
        {
            const bool warning = block.tone == "WARNING" || block.tone == "IMPORTANT";
            const bool error = block.tone == "ERROR" || block.tone == "CAUTION";
            const bool success = block.tone == "SUCCESS" || block.tone == "TIP";
            const winrt::Windows::UI::Color color = error ? winrt::Windows::UI::Color{ 40, 220, 65, 45 } : warning ? winrt::Windows::UI::Color{ 40, 220, 175, 0 } : success ? winrt::Windows::UI::Color{ 40, 40, 170, 95 } : winrt::Windows::UI::Color{ 40, 65, 130, 230 };
            WUX::Controls::Grid grid;
            WUX::Controls::ColumnDefinition iconColumn;
            iconColumn.Width(WUX::GridLength{ 28, WUX::GridUnitType::Pixel });
            grid.ColumnDefinitions().Append(iconColumn);
            WUX::Controls::ColumnDefinition bodyColumn;
            bodyColumn.Width(WUX::GridLength{ 1, WUX::GridUnitType::Star });
            grid.ColumnDefinitions().Append(bodyColumn);
            WUX::Controls::TextBlock icon;
            icon.Text(error ? L"⊗" : warning ? L"⚠" : success ? L"✓" : L"ⓘ");
            icon.FontSize(18);
            WUX::Automation::AutomationProperties::SetName(icon, winrt::to_hstring(block.tone));
            grid.Children().Append(icon);
            const auto body = Convert(block.text, baseUrl, depth + 1);
            WUX::Controls::Grid::SetColumn(body, 1);
            grid.Children().Append(body);
            WUX::Controls::Border card;
            card.Padding(WUX::Thickness{ 12, 10, 12, 10 });
            card.CornerRadius(WUX::CornerRadius{ 4, 4, 4, 4 });
            card.Background(WUX::Media::SolidColorBrush{ color });
            card.Child(grid);
            data._AppendBlock(card);
        }
        else
        {
            data._EndParagraph();
            unique_node doc{ cmark_parse_document(block.text.data(), block.text.size(), CMARK_OPT_DEFAULT) };
            unique_iter iter{ cmark_iter_new(doc.get()) };
            cmark_event_type ev_type;
            while ((ev_type = cmark_iter_next(iter.get())) != CMARK_EVENT_DONE)
                data._RenderNode(cmark_iter_get_node(iter.get()), ev_type);
        }
    }

    return data._root;
}

void MarkdownToXaml::_AppendBlock(const WUX::FrameworkElement& element)
{
    _EndParagraph();
    WUX::Documents::InlineUIContainer container;
    container.Child(element);
    _CurrentParagraph().Inlines().Append(container);
    _CurrentParagraph().Margin(WUX::Thickness{ 0, 8, 0, 8 });
    // Inline controls otherwise measure to their intrinsic width and can escape
    // the pane. Reflow them with the owning rich-text block on every resize.
    const auto resize = [root = winrt::make_weak(_root), child = winrt::make_weak(element), indent = _indent] {
        try
        {
            const auto owner = root.get();
            const auto content = child.get();
            if (owner && content && owner.ActualWidth() > 0)
                content.Width(std::max(1.0, owner.ActualWidth() - IndentWidth * indent));
        }
        CATCH_LOG();
    };
    _root.SizeChanged([resize](auto&&, auto&&) { resize(); });
    element.Loaded([resize](auto&&, auto&&) { resize(); });
    _EndParagraph();
}

MarkdownToXaml::MarkdownToXaml(const winrt::hstring& baseUrl) :
    _baseUri{ baseUrl }
{
    _root.ContextFlyout(winrt::Microsoft::Terminal::UI::TextMenuFlyout{});
    _root.IsTextSelectionEnabled(true);
    _root.TextWrapping(WUX::TextWrapping::WrapWholeWords);
}

WUX::Documents::Paragraph MarkdownToXaml::_CurrentParagraph()
{
    if (_lastParagraph == nullptr)
    {
        _lastParagraph = WUX::Documents::Paragraph{};
        if (_indent > 0)
        {
            // If we're in a list, we will start this paragraph with a bullet
            // point. That bullet point will be added as part of the actual text
            // of the paragraph, but we want the real text of the paragraph all
            // aligned. So we will _de-indent_ the first line, to give us space
            // for the bullet.
            if (_indent - _blockQuoteDepth > 0)
            {
                _lastParagraph.TextIndent(-WidthOfBulletPoint);
            }
            _lastParagraph.Margin(WUX::ThicknessHelper::FromLengths(static_cast<double>(IndentWidth) * _indent, 0, 0, 0));
        }
        _root.Blocks().Append(_lastParagraph);
    }
    return _lastParagraph;
}
WUX::Documents::Run MarkdownToXaml::_CurrentRun()
{
    if (_lastRun == nullptr)
    {
        _lastRun = WUX::Documents::Run{};
        _CurrentSpan().Inlines().Append(_lastRun);
    }
    return _lastRun;
}
WUX::Documents::Span MarkdownToXaml::_CurrentSpan()
{
    if (_lastSpan == nullptr)
    {
        _lastSpan = WUX::Documents::Span{};
        _CurrentParagraph().Inlines().Append(_lastSpan);
    }
    return _lastSpan;
}
WUX::Documents::Run MarkdownToXaml::_NewRun()
{
    if (_lastRun == nullptr)
    {
        return _CurrentRun();
    }
    else
    {
        auto old{ _lastRun };

        WUX::Documents::Run newRun{};

        newRun.FontFamily(old.FontFamily());
        newRun.FontWeight(old.FontWeight());
        newRun.FontStyle(old.FontStyle());

        _lastRun = newRun;
        _CurrentSpan().Inlines().Append(_lastRun);
    }
    return _lastRun;
}
void MarkdownToXaml::_EndRun() noexcept
{
    _lastRun = nullptr;
}
void MarkdownToXaml::_EndSpan() noexcept
{
    _EndRun();
    _lastSpan = nullptr;
}
void MarkdownToXaml::_EndParagraph() noexcept
{
    _EndSpan();
    _lastParagraph = nullptr;
}

WUX::Controls::TextBlock MarkdownToXaml::_makeDefaultTextBlock()
{
    WUX::Controls::TextBlock b{};
    b.ContextFlyout(winrt::Microsoft::Terminal::UI::TextMenuFlyout{});
    b.IsTextSelectionEnabled(true);
    b.TextWrapping(WUX::TextWrapping::WrapWholeWords);
    return b;
}

void MarkdownToXaml::_RenderNode(cmark_node* node, cmark_event_type ev_type)
{
    const bool entering = (ev_type == CMARK_EVENT_ENTER);

    switch (cmark_node_get_type(node))
    {
    case CMARK_NODE_DOCUMENT:
        break;

    case CMARK_NODE_BLOCK_QUOTE:

        // It's non-trivial to deal with the right-side vertical lines that
        // we're accustomed to seeing for block quotes in markdown content.
        // RichTextBlock doesn't have a good way of adding a border to a
        // paragraph, it would seem.
        //
        // We could add a InlineUIContainer, with a Border in there, then
        // put a new RichTextBlock in there, but I believe text selection
        // wouldn't transit across the border.

        // Instead, we're just going to add a new layer of indenting.

        if (entering)
        {
            _EndParagraph();
            _indent++;
            _blockQuoteDepth++;
        }
        else
        {
            _EndParagraph();
            _indent = std::max(0, _indent - 1);
            _blockQuoteDepth = std::max(0, _blockQuoteDepth - 1);
        }

        break;

    case CMARK_NODE_LIST:
    {
        // when `node->as.list.list_type == CMARK_BULLET_LIST`, we're an unordered list.
        // Otherwise, we're an ordered one (and we might not start at 0).
        //
        // However, we don't support numbered lists for now.
        if (entering)
        {
            _EndParagraph();
            _indent++;
        }
        else
        {
            _EndParagraph();
            _indent = std::max(0, _indent - 1);
        }
        break;
    }

    case CMARK_NODE_ITEM:
        // A list item, either for an ordered list or an unordered one.
        if (entering)
        {
            _EndParagraph();
            _NewRun().Text(gsl::at(bullets, std::clamp(_indent - _blockQuoteDepth - 1, 0, 2)));
        }
        break;

    case CMARK_NODE_HEADING:
    {
        _EndParagraph();

        // At the start of a header, change the font size to match the new
        // level of header we're at. The text will come later, in a
        // CMARK_NODE_TEXT
        if (entering)
        {
            // Insert a blank line, just to help break up the walls of text.
            // This better reflects the way MD is rendered to HTML
            _root.Blocks().Append(WUX::Documents::Paragraph{});

            const auto level = cmark_node_get_heading_level(node);
            _CurrentParagraph().FontSize(std::max(HeaderMinFontSize, H1FontSize - level * 6));
        }
        break;
    }

    case CMARK_NODE_CODE_BLOCK:
    {
        _EndParagraph();

        const auto codeHstring{ winrt::to_hstring(cmark_node_get_literal(node)) };
        // The literal for a code node always includes the trailing newline.
        // Trim that off.
        const std::wstring_view codeView{ codeHstring.c_str(), codeHstring.empty() ? 0 : codeHstring.size() - 1 };

        auto codeBlock = winrt::make<winrt::Microsoft::Terminal::UI::Markdown::implementation::CodeBlock>(winrt::hstring{ codeView }, winrt::to_hstring(cmark_node_get_fence_info(node) ? cmark_node_get_fence_info(node) : ""));
        _AppendBlock(codeBlock);
    }
    break;

    case CMARK_NODE_HTML_BLOCK:
        // Raw HTML comes to us in the literal
        //    node->as.literal.data, node->as.literal.len
        // But we don't support raw HTML, so we'll do nothing.
        break;

    case CMARK_NODE_CUSTOM_BLOCK:
        // Not even entirely sure what this is.
        break;

    case CMARK_NODE_THEMATIC_BREAK:
        // A <hr>. Not currently supported.
        break;

    case CMARK_NODE_PARAGRAPH:
    {
        bool tight;
        cmark_node* parent = cmark_node_parent(node);
        cmark_node* grandparent = cmark_node_parent(parent);

        if (grandparent != nullptr && cmark_node_get_type(grandparent))
        {
            tight = cmark_node_get_list_tight(grandparent);
        }
        else
        {
            tight = false;
        }

        // If we aren't in a list, then end the current paragraph and
        // start a new one.
        if (!tight)
        {
            _EndParagraph();
        }

        // Start a new paragraph if we don't have one
        break;
    }
    case CMARK_NODE_TEXT:
    {
        const auto text{ winrt::to_hstring(textFromLiteral(node)) };

        if (_lastImage)
        {
            // The tooltip for an image comes in as a CMARK_NODE_TEXT, so set that here.
            WUX::Controls::ToolTipService::SetToolTip(_lastImage, box_value(text));
        }
        else
        {
            // Otherwise, just add the text to the current paragraph
            _NewRun().Text(text);
        }
    }

    break;

    case CMARK_NODE_LINEBREAK:
        _EndSpan();
        _CurrentParagraph().Inlines().Append(WUX::Documents::LineBreak());
        break;

    case CMARK_NODE_SOFTBREAK:
        // I'm fairly confident this is what happens when you've just got
        // two lines only separated by a single \r\n in a MD doc. E.g. when
        // you want a paragraph to wrap at 80 columns in code, but wrap in
        // the rendered document.
        //
        // In the HTML implementation, what happens here depends on the options:
        // * CMARK_OPT_HARDBREAKS: add a full line break
        // * CMARK_OPT_NOBREAKS: Just add a space
        // * otherwise, just add a '\n'
        //
        // We're not really messing with options here, so lets just add a
        // space. That seems to keep the current line going, but allow for
        // word breaking.

        _NewRun().Text(L" ");
        break;

    case CMARK_NODE_CODE:
    {
        const auto text{ winrt::to_hstring(textFromLiteral(node)) };
        const auto& codeRun{ _NewRun() };

        codeRun.FontFamily(WUX::Media::FontFamily{ CodeFontFamily });
        // A Span can't have a border or a background, so we can't give
        // it the whole treatment that a <code> span gets in HTML.
        codeRun.Text(text);

        _NewRun().FontFamily(_root.FontFamily());
    }
    break;

    case CMARK_NODE_HTML_INLINE:
        // Same as above - no raw HTML support here.
        break;

    case CMARK_NODE_CUSTOM_INLINE:
        // Same as above - not even entirely sure what this is.
        break;

    case CMARK_NODE_STRONG:
        _NewRun().FontWeight(entering ?
                                 winrt::Windows::UI::Text::FontWeights::Bold() :
                                 winrt::Windows::UI::Text::FontWeights::Normal());
        break;

    case CMARK_NODE_EMPH:
        _NewRun().FontStyle(entering ?
                                winrt::Windows::UI::Text::FontStyle::Italic :
                                winrt::Windows::UI::Text::FontStyle::Normal);
        break;

    case CMARK_NODE_LINK:

        if (entering)
        {
            const auto urlHstring{ to_hstring(textFromUrl(node)) };
            WUX::Documents::Hyperlink a{};

            // Set the tooltip to display the URL
            try
            {
                // This block is from TermControl.cpp, where we sanitize the
                // tooltips for URLs. That has a much more comprehensive
                // comment.

                const winrt::Windows::Foundation::Uri uri{ _baseUri, urlHstring };

                a.NavigateUri(uri);

                auto tooltipText = urlHstring;
                const auto unicode = uri.AbsoluteUri();
                const auto punycode = uri.AbsoluteCanonicalUri();
                if (punycode != unicode)
                {
                    tooltipText = winrt::hstring{ punycode + L"\n" + unicode };
                }
                WUX::Controls::ToolTipService::SetToolTip(a, box_value(tooltipText));
            }
            catch (...)
            {
            }

            _CurrentParagraph().Inlines().Append(a);
            _lastSpan = a;

            // Similar to the header element, the actual text of the link
            // will later come through as a CMARK_NODE_TEXT
        }
        else
        {
            _EndSpan();
        }
        break;

    case CMARK_NODE_IMAGE:
        if (entering)
        {
            const auto urlHstring{ to_hstring(textFromUrl(node)) };

            try
            {
                winrt::Windows::Foundation::Uri uri{ _baseUri, urlHstring };

                WUX::Media::Imaging::BitmapImage bitmapImage;
                bitmapImage.UriSource(uri);

                WUX::Controls::Image img{};
                img.Source(bitmapImage);

                WUX::Documents::InlineUIContainer imageBlock{};
                imageBlock.Child(img);

                _CurrentParagraph().Inlines().Append(imageBlock);
                _lastImage = img;
            }
            catch (...)
            {
            }
        }
        else
        {
            _EndSpan();
            _lastImage = nullptr;
        }
        break;

        // These elements are in cmark-gfm, which we'd love to move to in the
        // future, but isn't yet available in vcpkg.

        // case CMARK_NODE_FOOTNOTE_DEFINITION:
        //     // Not supported currently
        //     break;
        //
        // case CMARK_NODE_FOOTNOTE_REFERENCE:
        //     // Not supported currently
        //     break;

    default:
        assert(false);
        break;
    }
}
