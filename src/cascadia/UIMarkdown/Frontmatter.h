// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace MarkdownPreview
{
    struct MetadataRow { std::string key; std::string value; size_t depth; };
    struct Frontmatter
    {
        std::string_view body;
        std::string_view yaml;
        std::vector<MetadataRow> rows;
        std::string error;
        bool present = false;
    };

    inline Frontmatter ParseFrontmatter(std::string_view text)
    {
        Frontmatter result{ text, {}, {}, {}, false };
        if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
        const auto line = [&](size_t start) {
            auto value = text.substr(start, text.find('\n', start) - start);
            while (!value.empty() && (value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
            return value;
        };
        if (line(0) != "---") return result;
        const auto first = text.find('\n');
        if (first == text.npos) return result;
        for (size_t start = first + 1; start < text.size();)
        {
            const auto end = text.find('\n', start);
            if (line(start) == "---" || line(start) == "...")
            {
                result.present = true;
                result.yaml = text.substr(first + 1, start - first - 1);
                result.body = end == text.npos ? text.substr(text.size()) : text.substr(end + 1);
                break;
            }
            if (end == text.npos) break;
            start = end + 1;
        }
        if (!result.present) return result; // An unclosed delimiter remains ordinary Markdown.
        if (result.yaml.size() > 65536) { result.error = "Frontmatter exceeds the 64 KiB metadata limit. View Raw for the original."; return result; }
        try
        {
            const auto node = YAML::Load(std::string{ result.yaml });
            size_t budget = 200;
            const auto visit = [&](auto&& self, const YAML::Node& current, std::string key, size_t depth) -> void {
                if (budget == 0) return;
                --budget;
                if (depth >= 12) { result.rows.push_back({ std::move(key), "[Nested metadata limit reached]", depth }); return; }
                if (current.IsScalar() || current.IsNull()) result.rows.push_back({ std::move(key), current.IsNull() ? "null" : current.Scalar(), depth });
                else
                {
                    if (!key.empty()) result.rows.push_back({ std::move(key), current.size() == 0 ? (current.IsMap() ? "{}" : "[]") : "", depth });
                    if (current.IsMap())
                    {
                        for (const auto& entry : current) { if (!budget) break; self(self, entry.second, entry.first.IsScalar() ? entry.first.Scalar() : "[Complex key]", depth + 1); }
                    }
                    else if (current.IsSequence())
                    {
                        for (const auto& entry : current) { if (!budget) break; self(self, entry, "•", depth + 1); }
                    }
                }
            };
            visit(visit, node, "", 0);
            if (!budget) result.rows.push_back({ "", "[Metadata truncated to 200 entries]", 0 });
        }
        catch (const YAML::Exception& error) { result.error = std::string{ "Invalid YAML frontmatter: " } + error.what(); }
        return result;
    }
}
