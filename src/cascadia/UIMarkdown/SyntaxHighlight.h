// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
#pragma once
#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace MarkdownPreview
{
    enum class TokenKind { Plain, Keyword, String, Number, Comment, Key, Markup };
    struct Token { size_t start; size_t length; TokenKind kind; };

    inline std::wstring Language(std::wstring_view info)
    {
        auto value = std::wstring{ info.substr(0, info.find_first_of(L" \t\r\n")) };
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if (value == L"yml") return L"yaml";
        if (value == L"js" || value == L"jsx" || value == L"ts" || value == L"tsx") return L"javascript";
        if (value == L"py") return L"python";
        if (value == L"sh" || value == L"shell" || value == L"zsh") return L"bash";
        if (value == L"ps1" || value == L"pwsh") return L"powershell";
        if (value == L"md") return L"markdown";
        if (value == L"c++" || value == L"cc" || value == L"cxx" || value == L"h" || value == L"hpp") return L"cpp";
        if (value == L"cs" || value == L"c#") return L"csharp";
        if (value == L"rs") return L"rust";
        if (value == L"html" || value == L"svg") return L"xml";
        return value;
    }

    // A bounded lexical highlighter. Unknown fence languages remain verbatim plain text.
    // Tokens cover the source exactly; coloring must never alter copied code.
    inline std::vector<Token> Highlight(std::wstring_view text, std::wstring_view info)
    {
        std::vector<Token> tokens;
        const auto lang = Language(info);
        const bool yaml = lang == L"yaml", json = lang == L"json" || lang == L"jsonc";
        const bool python = lang == L"python", shell = lang == L"bash" || lang == L"powershell";
        const bool markdown = lang == L"markdown", xml = lang == L"xml", sql = lang == L"sql";
        const bool cstyle = lang == L"cpp" || lang == L"c" || lang == L"javascript" || lang == L"typescript" || lang == L"java" || lang == L"csharp" || lang == L"rust" || lang == L"go" || lang == L"css";
        if (!(yaml || json || python || shell || markdown || xml || sql || cstyle)) return { { 0, text.size(), TokenKind::Plain } };
        const auto add = [&](size_t start, size_t end, TokenKind kind) {
            if (!tokens.empty() && tokens.back().kind == kind) tokens.back().length += end - start;
            else tokens.push_back({ start, end - start, kind });
        };
        const auto endOfLine = [&](size_t start) { const auto end = text.find(L'\n', start); return end == text.npos ? text.size() : end; };
        const auto after = [&](size_t start, std::wstring_view closing) { const auto end = text.find(closing, start); return end == text.npos ? text.size() : end + closing.size(); };
        const auto wordChar = [](wchar_t c) { return std::iswalnum(c) || c == L'_' || c == L'$'; };
        constexpr std::wstring_view keywords = L"|if|else|elif|for|foreach|while|do|switch|case|break|continue|return|yield|throw|try|catch|finally|class|struct|enum|interface|namespace|using|import|from|as|export|default|public|private|protected|static|const|let|var|auto|void|int|float|double|bool|char|string|new|delete|this|self|def|lambda|async|await|function|fn|pub|impl|trait|match|mut|use|mod|package|func|type|defer|go|select|in|is|not|and|or|pass|with|raise|except|extends|implements|readonly|type|typename|template|sizeof|include|define|echo|then|fi|done|esac|where|select|insert|update|delete|into|values|join|on|order|by|group|having|limit|create|table|primary|key|";
        for (size_t i = 0; i < text.size();)
        {
            if (tokens.size() >= 16000) { tokens.push_back({ i, text.size() - i, TokenKind::Plain }); break; }
            const auto start = i;
            const auto c = text[i];
            const auto remaining = text.substr(i);
            auto kind = TokenKind::Plain;
            if (((yaml || python || shell) && c == L'#') || ((cstyle || lang == L"jsonc") && remaining.starts_with(L"//")) || (sql && remaining.starts_with(L"--")))
            { i = endOfLine(i); kind = TokenKind::Comment; }
            else if ((cstyle || lang == L"jsonc" || sql) && remaining.starts_with(L"/*"))
            { i = after(i + 2, L"*/"); kind = TokenKind::Comment; }
            else if ((xml || markdown) && remaining.starts_with(L"<!--"))
            { i = after(i + 4, L"-->"); kind = TokenKind::Comment; }
            else if (python && (remaining.starts_with(L"\"\"\"") || remaining.starts_with(L"'''")))
            { i = after(i + 3, remaining.substr(0, 3)); kind = TokenKind::String; }
            else if (c == L'\"' || c == L'\'' || (c == L'`' && (markdown || lang == L"javascript" || lang == L"typescript")))
            {
                ++i;
                while (i < text.size())
                {
                    if (text[i] == L'\\' && i + 1 < text.size() && !(yaml && c == L'\'')) { i += 2; continue; }
                    if (text[i++] == c)
                    {
                        if ((yaml || sql) && i < text.size() && text[i] == c) { ++i; continue; }
                        break;
                    }
                }
                kind = TokenKind::String;
                auto next = text.find_first_not_of(L" \t", i);
                if ((json || yaml) && next != text.npos && text[next] == L':') kind = TokenKind::Key;
            }
            else if (xml && c == L'<') { i = after(i + 1, L">"); kind = TokenKind::Markup; }
            else if (markdown && (i == 0 || text[i - 1] == L'\n') && (c == L'#' || remaining.starts_with(L"---") || remaining.starts_with(L"```")))
            { i = endOfLine(i); kind = TokenKind::Markup; }
            else if (std::iswdigit(c))
            {
                ++i;
                while (i < text.size() && (std::iswalnum(text[i]) || text[i] == L'.' || text[i] == L'_')) ++i;
                kind = TokenKind::Number;
            }
            else if (wordChar(c))
            {
                ++i;
                while (i < text.size() && (wordChar(text[i]) || (yaml && text[i] == L'-'))) ++i;
                auto word = std::wstring{ text.substr(start, i - start) };
                if (sql) std::transform(word.begin(), word.end(), word.begin(), [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
                const auto next = text.find_first_not_of(L" \t", i);
                if (yaml && next != text.npos && text[next] == L':' && (next + 1 == text.size() || std::iswspace(text[next + 1]))) kind = TokenKind::Key;
                else if (word == L"true" || word == L"false" || word == L"null" || word == L"True" || word == L"False" || word == L"None" || word == L"undefined" || word == L"nil") kind = TokenKind::Keyword;
                else if (!yaml && !json && !xml && !markdown && keywords.find(L"|" + word + L"|") != keywords.npos) kind = TokenKind::Keyword;
                else if (shell && c == L'$') kind = TokenKind::Key;
            }
            else ++i;
            add(start, i, kind);
        }
        return tokens;
    }
}
