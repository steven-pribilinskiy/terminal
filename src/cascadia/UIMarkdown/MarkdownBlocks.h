// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace MarkdownPreview
{
    struct RichBlock
    {
        enum class Kind { Markdown, Table, Callout } kind = Kind::Markdown;
        std::string text;
        std::string tone;
        std::vector<std::vector<std::string>> rows;
    };

    inline std::string_view Trim(std::string_view text)
    {
        const auto start = text.find_first_not_of(" \t\r");
        if (start == text.npos) return {};
        return text.substr(start, text.find_last_not_of(" \t\r") - start + 1);
    }

    inline std::vector<std::string> TableCells(std::string_view line)
    {
        line = Trim(line);
        if (line.starts_with('|')) line.remove_prefix(1);
        if (line.ends_with('|') && (line.size() < 2 || line[line.size() - 2] != '\\')) line.remove_suffix(1);
        std::vector<std::string> cells;
        std::string cell;
        size_t codeFence = 0;
        for (size_t i = 0; i < line.size(); ++i)
        {
            if (line[i] == '\\' && i + 1 < line.size() && line[i + 1] == '|') { cell += '|'; ++i; }
            else if (line[i] == '`')
            {
                size_t count = 1;
                while (i + count < line.size() && line[i + count] == '`') ++count;
                if (!codeFence) codeFence = count;
                else if (codeFence == count) codeFence = 0;
                cell.append(count, '`');
                i += count - 1;
            }
            else if (line[i] == '|' && !codeFence) { cells.emplace_back(Trim(cell)); cell.clear(); }
            else cell += line[i];
        }
        cells.emplace_back(Trim(cell));
        return cells;
    }

    inline bool TableDivider(std::string_view line)
    {
        if (line.find('|') == line.npos) return false;
        const auto cells = TableCells(line);
        return !cells.empty() && std::all_of(cells.begin(), cells.end(), [](const auto& cell) {
            auto value = Trim(cell);
            if (value.starts_with(':')) value.remove_prefix(1);
            if (value.ends_with(':')) value.remove_suffix(1);
            return value.size() >= 3 && value.find_first_not_of('-') == value.npos;
        });
    }

    // Extract extensions before CommonMark parsing. Fenced source stays opaque.
    inline std::vector<RichBlock> ParseRichBlocks(std::string_view source)
    {
        std::vector<std::string_view> lines;
        for (size_t pos = 0; pos < source.size();)
        {
            const auto end = source.find('\n', pos);
            lines.push_back(source.substr(pos, end == source.npos ? source.size() - pos : end - pos));
            if (end == source.npos) break;
            pos = end + 1;
        }
        std::vector<RichBlock> blocks;
        std::string ordinary;
        char fence = 0;
        size_t fenceLength = 0;
        const auto flush = [&] {
            if (!ordinary.empty()) { RichBlock block; block.text = std::move(ordinary); blocks.push_back(std::move(block)); ordinary.clear(); }
        };
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const auto line = Trim(lines[i]);
            const auto fenceEnd = line.find_first_not_of(line.empty() ? '\0' : line.front());
            const auto run = fenceEnd == line.npos ? line.size() : fenceEnd;
            if (line.size() >= 3 && (line.front() == '`' || line.front() == '~') && run >= 3)
            {
                if (!fence) { fence = line.front(); fenceLength = run; }
                else if (line.front() == fence && run >= fenceLength && Trim(line.substr(run)).empty()) fence = 0;
                ordinary.append(lines[i]); ordinary += '\n'; continue;
            }
            if (!fence && line.starts_with("> [!") && line.ends_with(']') && blocks.size() < 1000)
            {
                const auto tone = line.substr(4, line.size() - 5);
                if (tone == "NOTE" || tone == "INFO" || tone == "TIP" || tone == "SUCCESS" || tone == "WARNING" || tone == "CAUTION" || tone == "ERROR" || tone == "IMPORTANT")
                {
                    flush();
                    RichBlock block; block.kind = RichBlock::Kind::Callout; block.tone = tone;
                    while (i + 1 < lines.size() && Trim(lines[i + 1]).starts_with('>'))
                    {
                        auto inner = Trim(lines[++i]); inner.remove_prefix(1);
                        if (inner.starts_with(' ')) inner.remove_prefix(1);
                        block.text.append(inner); block.text += '\n';
                    }
                    blocks.push_back(std::move(block)); continue;
                }
            }
            if (!fence && i + 1 < lines.size() && line.find('|') != line.npos && TableDivider(lines[i + 1]) && blocks.size() < 1000)
            {
                flush();
                RichBlock block; block.kind = RichBlock::Kind::Table; block.rows.push_back(TableCells(line)); ++i;
                while (i + 1 < lines.size() && !Trim(lines[i + 1]).empty() && lines[i + 1].find('|') != line.npos && block.rows.size() < 201)
                    block.rows.push_back(TableCells(lines[++i]));
                blocks.push_back(std::move(block)); continue;
            }
            ordinary.append(lines[i]); ordinary += '\n';
        }
        flush();
        return blocks;
    }
}
