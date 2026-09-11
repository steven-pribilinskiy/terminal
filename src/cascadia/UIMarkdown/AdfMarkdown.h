// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
#pragma once
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <string>
#include <algorithm>

namespace MarkdownPreview
{
    using namespace winrt::Windows::Data::Json;
    inline void AppendText(std::wstring& out, const winrt::hstring& text) { out.append(text.data(), text.size()); }

    inline void FlattenAdf(const IJsonValue& node, std::wstring& out, int depth);

    inline void FlattenAdfChildren(const JsonObject& object, std::wstring& out, int depth)
    {
        const auto content = object.GetNamedArray(L"content", nullptr);
        if (!content)
        {
            return;
        }
        for (uint32_t i = 0; i < content.Size(); ++i)
        {
            FlattenAdf(content.GetAt(i), out, depth + 1);
        }
    }

    // Atlassian Document Format -> Markdown. ADF is a JSON document tree, not
    // markup, so there is nothing a text renderer could be handed directly.
    // This walks it far enough to read a Jira description or comment and no
    // further: an unrecognised node contributes its children and nothing else,
    // and recursion and output are bounded.
    inline void FlattenAdf(const IJsonValue& node, std::wstring& out, int depth)
    {
        // Bounded on both axes: hand-written JSON can nest arbitrarily, and a
        // description long enough to matter is already longer than any surface
        // showing it wants to render.
        if (!node || depth > 16 || out.size() > 20000)
        {
            return;
        }
        if (node.ValueType() == JsonValueType::String)
        {
            AppendText(out, node.GetString());
            return;
        }
        if (node.ValueType() != JsonValueType::Object)
        {
            return;
        }

        const auto object = node.GetObject();
        const std::wstring type{ object.GetNamedString(L"type", L"") };

        const auto endLine = [&out]() {
            if (!out.empty() && out.back() != L'\n')
            {
                out.push_back(L'\n');
            }
        };

        if (type == L"text")
        {
            // ADF puts styling in a "marks" array beside the text rather than in
            // the text itself, so this is where bold, italic, code and links turn
            // back into the markdown that says the same thing.
            //
            // Order matters on the way out: the wrappers have to close in the
            // reverse of the order they opened, or the emphasis markers nest
            // wrongly and the renderer shows them as literal asterisks.
            auto text = std::wstring{ object.GetNamedString(L"text", L"") };
            if (text.empty())
            {
                return;
            }

            std::wstring href;
            std::wstring prefix;
            std::wstring suffix;

            if (const auto marks = object.GetNamedArray(L"marks", nullptr))
            {
                for (uint32_t i = 0; i < marks.Size(); ++i)
                {
                    const auto entry = marks.GetAt(i);
                    if (!entry || entry.ValueType() != JsonValueType::Object)
                    {
                        continue;
                    }
                    const auto mark = entry.GetObject();
                    const std::wstring markType{ mark.GetNamedString(L"type", L"") };

                    // Code wins outright: markdown has no way to bold something
                    // inside a code span, and asterisks within one are literal.
                    if (markType == L"code")
                    {
                        prefix = L"`";
                        suffix = L"`";
                        href.clear();
                        break;
                    }
                    if (markType == L"strong")
                    {
                        prefix += L"**";
                        suffix.insert(0, L"**");
                    }
                    else if (markType == L"em")
                    {
                        prefix += L"*";
                        suffix.insert(0, L"*");
                    }
                    else if (markType == L"strike")
                    {
                        prefix += L"~~";
                        suffix.insert(0, L"~~");
                    }
                    else if (markType == L"link")
                    {
                        if (const auto attrs = mark.GetNamedObject(L"attrs", nullptr))
                        {
                            href = attrs.GetNamedString(L"href", L"");
                        }
                    }
                }
            }

            if (!href.empty())
            {
                AppendText(out, winrt::hstring{ (L"[" + prefix + text + suffix + L"](" + href + L")") });
                return;
            }

            AppendText(out, winrt::hstring{ prefix + text + suffix });
            return;
        }
        if (type == L"hardBreak")
        {
            out.push_back(L'\n');
            return;
        }
        if (type == L"mention" || type == L"emoji")
        {
            if (const auto attrs = object.GetNamedObject(L"attrs", nullptr))
            {
                auto label = attrs.GetNamedString(L"text", L"");
                if (label.empty())
                {
                    label = attrs.GetNamedString(L"shortName", L"");
                }
                AppendText(out, label);
            }
            return;
        }
        if (type == L"rule")
        {
            endLine();
            out.append(L"---\n");
            return;
        }
        if (type == L"codeBlock")
        {
            endLine();
            std::wstring language;
            if (const auto attrs = object.GetNamedObject(L"attrs", nullptr)) language = attrs.GetNamedString(L"language", L"");
            std::erase_if(language, [](wchar_t c) { return !(c >= L'a' && c <= L'z') && !(c >= L'A' && c <= L'Z') && !(c >= L'0' && c <= L'9') && c != L'+' && c != L'#' && c != L'-'; });
            out.append(L"```" + language + L"\n");
            FlattenAdfChildren(object, out, depth);
            endLine();
            out.append(L"```\n");
            return;
        }
        if (type == L"panel")
        {
            std::wstring tone = L"NOTE";
            if (const auto attrs = object.GetNamedObject(L"attrs", nullptr))
            {
                const auto panelType = attrs.GetNamedString(L"panelType", L"info");
                if (panelType == L"warning") tone = L"WARNING";
                else if (panelType == L"error") tone = L"ERROR";
                else if (panelType == L"success") tone = L"SUCCESS";
                else if (panelType == L"note") tone = L"IMPORTANT";
            }
            std::wstring content;
            FlattenAdfChildren(object, content, depth);
            endLine(); out.append(L"\n> [!" + tone + L"]\n");
            for (size_t pos = 0; pos < content.size();)
            {
                const auto end = content.find(L'\n', pos);
                out.append(L"> "); out.append(content, pos, end == content.npos ? content.size() - pos : end - pos); out += L'\n';
                if (end == content.npos) break;
                pos = end + 1;
            }
            out += L'\n';
            return;
        }
        if (type == L"table")
        {
            const auto rows = object.GetNamedArray(L"content", nullptr);
            if (!rows) return;
            endLine(); out += L'\n';
            size_t columns = 0;
            for (uint32_t row = 0; row < rows.Size() && row < 200 && out.size() < 20000; ++row)
            {
                const auto rowValue = rows.GetAt(row);
                if (rowValue.ValueType() != JsonValueType::Object) continue;
                const auto cells = rowValue.GetObject().GetNamedArray(L"content", nullptr);
                if (!cells) continue;
                if (!columns) columns = std::min<uint32_t>(32, cells.Size());
                out += L'|';
                for (size_t col = 0; col < columns; ++col)
                {
                    std::wstring content;
                    if (col < cells.Size()) FlattenAdf(cells.GetAt(static_cast<uint32_t>(col)), content, depth + 2);
                    while (!content.empty() && (content.back() == L'\n' || content.back() == L'\r')) content.pop_back();
                    out += L' ';
                    for (const auto c : content)
                    {
                        if (c == L'|') out.append(L"\\|");
                        else if (c == L'\n') out.append(L"<br>");
                        else if (c != L'\r') out += c;
                    }
                    out.append(L" |");
                }
                out += L'\n';
                if (row == 0)
                {
                    out += L'|';
                    for (size_t col = 0; col < columns; ++col) out.append(L" --- |");
                    out += L'\n';
                }
            }
            out += L'\n';
            return;
        }
        if (type == L"bulletList" || type == L"orderedList")
        {
            const auto items = object.GetNamedArray(L"content", nullptr);
            if (items)
            {
                const auto ordered = type == L"orderedList";
                for (uint32_t i = 0; i < items.Size(); ++i)
                {
                    endLine();
                    out.append(ordered ? (std::to_wstring(i + 1) + L". ") : std::wstring{ L"- " });
                    FlattenAdf(items.GetAt(i), out, depth + 1);
                    endLine();
                }
            }
            return;
        }

        // Blocks that markdown marks with a prefix rather than a wrapper. The
        // prefix goes on before the children write, and only once, because ADF
        // nests the inline content one level down inside a paragraph.
        if (type == L"heading")
        {
            endLine();
            auto level = 2;
            if (const auto attrs = object.GetNamedObject(L"attrs", nullptr))
            {
                level = static_cast<int>(attrs.GetNamedNumber(L"level", 2));
                level = level < 1 ? 1 : (level > 6 ? 6 : level);
            }
            out.append(static_cast<size_t>(level), L'#');
            out.push_back(L' ');
            FlattenAdfChildren(object, out, depth);
            endLine();
            return;
        }
        if (type == L"blockquote")
        {
            endLine();
            out.append(L"> ");
            FlattenAdfChildren(object, out, depth);
            endLine();
            return;
        }

        FlattenAdfChildren(object, out, depth);

        // Everything ADF calls a block ends the line it wrote.
        if (type == L"paragraph" || type == L"listItem" ||
            type == L"panel" || type == L"tableRow")
        {
            endLine();
        }
    }

}
