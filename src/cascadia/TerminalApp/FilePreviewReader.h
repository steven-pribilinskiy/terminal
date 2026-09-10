// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
#pragma once
#include <msopc.h>
#include <xmllite.h>
#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <winrt/base.h>
#include <wil/resource.h>

// This reader is linked through a static library as well as directly by tests.
#pragma comment(lib, "xmllite.lib")
#pragma comment(lib, "urlmon.lib")

namespace Microsoft::Terminal::FilePreview
{
    using winrt::check_hresult;
    using winrt::com_ptr;
    using winrt::hresult_error;
    constexpr size_t TextLimit = 1024 * 1024;
    constexpr size_t PackageLimit = 32 * 1024 * 1024;
    struct XmlNode
    {
        std::wstring name, text;
        std::map<std::wstring, std::wstring> attributes;
        std::vector<std::unique_ptr<XmlNode>> children;
    };

    // Only package-local XML is parsed. DTDs and external relationships are never resolved.
    inline XmlNode ReadXml(IStream* stream, size_t& remaining)
    {
        STATSTG stat{};
        check_hresult(stream->Stat(&stat, STATFLAG_NONAME));
        if (stat.cbSize.QuadPart > remaining) throw hresult_error(E_FAIL, L"Document exceeds the preview extraction limit.");
        remaining -= static_cast<size_t>(stat.cbSize.QuadPart);
        com_ptr<IXmlReader> reader;
        check_hresult(CreateXmlReader(__uuidof(IXmlReader), reader.put_void(), nullptr));
        check_hresult(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit));
        check_hresult(reader->SetProperty(XmlReaderProperty_MaxElementDepth, 128));
        check_hresult(reader->SetInput(stream));
        XmlNode root;
        std::vector<XmlNode*> stack{ &root };
        XmlNodeType type{};
        HRESULT hr;
        size_t nodes = 0;
        while ((hr = reader->Read(&type)) == S_OK)
        {
            const wchar_t* value{};
            UINT length{};
            if (type == XmlNodeType_Element)
            {
                if (++nodes > 200000) throw hresult_error(E_FAIL, L"Document has too many elements to preview.");
                auto node = std::make_unique<XmlNode>();
                check_hresult(reader->GetLocalName(&value, &length));
                node->name.assign(value, length);
                const bool empty = reader->IsEmptyElement() != FALSE;
                if (reader->MoveToFirstAttribute() == S_OK)
                {
                    do
                    {
                        check_hresult(reader->GetLocalName(&value, &length));
                        std::wstring key{ value, length };
                        check_hresult(reader->GetPrefix(&value, &length));
                        if (key == L"id" && length != 0) key = L"relId";
                        check_hresult(reader->GetValue(&value, &length));
                        node->attributes[key] = std::wstring{ value, length };
                    } while (reader->MoveToNextAttribute() == S_OK);
                    check_hresult(reader->MoveToElement());
                }
                auto ptr = node.get();
                stack.back()->children.push_back(std::move(node));
                if (!empty) stack.push_back(ptr);
            }
            else if (type == XmlNodeType_EndElement)
            {
                if (stack.size() > 1) stack.pop_back();
            }
            else if (type == XmlNodeType_Text || type == XmlNodeType_CDATA || type == XmlNodeType_Whitespace)
            {
                check_hresult(reader->GetValue(&value, &length));
                stack.back()->text.append(value, length);
            }
        }
        check_hresult(hr);
        return root;
    }

    template<typename F> void Walk(const XmlNode& node, const F& callback)
    {
        callback(node);
        for (const auto& child : node.children) Walk(*child, callback);
    }

    inline std::wstring TextRuns(const XmlNode& node)
    {
        std::wstring result;
        Walk(node, [&](const XmlNode& n) { if (n.name == L"t" && result.size() < TextLimit) result += n.text.substr(0, TextLimit - result.size()); });
        return result;
    }

    inline void Paragraphs(const XmlNode& node, std::wstring& out)
    {
        if (out.size() >= TextLimit) return;
        if (node.name == L"t") out += node.text.substr(0, TextLimit - out.size());
        if (node.name == L"tab") out += L"\t";
        if (node.name == L"br") out += L"\n";
        for (const auto& child : node.children) Paragraphs(*child, out);
        if (node.name == L"p" || node.name == L"tr") out += L"\n";
        if (node.name == L"tc") out += L"\t";
    }

    inline std::wstring Attribute(const XmlNode& node, const wchar_t* key)
    {
        const auto it = node.attributes.find(key);
        return it == node.attributes.end() ? std::wstring{} : it->second;
    }

    inline std::vector<std::pair<std::wstring, std::wstring>> ReadOfficeSections(const std::wstring& path, const std::wstring& extension)
    {
        com_ptr<IOpcFactory> factory;
        check_hresult(CoCreateInstance(__uuidof(OpcFactory), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IOpcFactory), factory.put_void()));
        com_ptr<IStream> input;
        check_hresult(factory->CreateStreamOnFile(path.c_str(), OPC_STREAM_IO_READ, nullptr, FILE_ATTRIBUTE_NORMAL, input.put()));
        STATSTG inputStat{};
        check_hresult(input->Stat(&inputStat, STATFLAG_NONAME));
        if (inputStat.cbSize.QuadPart > 50 * 1024 * 1024) throw hresult_error(E_FAIL, L"File exceeds the 50 MiB preview limit.");
        com_ptr<IOpcPackage> package;
        check_hresult(factory->ReadPackageFromStream(input.get(), OPC_READ_DEFAULT, package.put()));
        com_ptr<IOpcPartSet> parts;
        check_hresult(package->GetPartSet(parts.put()));
        size_t remaining = PackageLimit;
        auto readPart = [&](const std::wstring& name) {
            com_ptr<IOpcPartUri> uri;
            check_hresult(factory->CreatePartUri(name.c_str(), uri.put()));
            com_ptr<IOpcPart> part;
            check_hresult(parts->GetPart(uri.get(), part.put()));
            com_ptr<IStream> stream;
            check_hresult(part->GetContentStream(stream.put()));
            return ReadXml(stream.get(), remaining);
        };
        std::vector<std::pair<std::wstring, std::wstring>> sections;
        size_t outputRemaining = TextLimit;
        auto add = [&](const std::wstring& label, std::wstring text) {
            if (outputRemaining == 0) return;
            const auto length = std::min(text.size(), outputRemaining);
            const bool truncated = text.size() >= outputRemaining;
            text.resize(length);
            outputRemaining -= length;
            if (truncated) text += L"\n[Preview truncated]";
            sections.emplace_back(label, std::move(text));
        };
        if (extension == L".docx")
        {
            auto xml = readPart(L"/word/document.xml");
            std::wstring text;
            Paragraphs(xml, text);
            add(L"Document", std::move(text));
        }
        else
        {
            const bool sheet = extension == L".xlsx";
            const std::wstring base = sheet ? L"/xl/" : L"/ppt/";
            auto main = readPart(base + (sheet ? L"workbook.xml" : L"presentation.xml"));
            std::map<std::wstring, std::wstring> targets;
            com_ptr<IOpcPartUri> mainUri;
            check_hresult(factory->CreatePartUri((base + (sheet ? L"workbook.xml" : L"presentation.xml")).c_str(), mainUri.put()));
            com_ptr<IOpcPart> mainPart;
            check_hresult(parts->GetPart(mainUri.get(), mainPart.put()));
            com_ptr<IOpcRelationshipSet> relationships;
            check_hresult(mainPart->GetRelationshipSet(relationships.put()));
            com_ptr<IOpcRelationshipEnumerator> enumerator;
            check_hresult(relationships->GetEnumerator(enumerator.put()));
            BOOL hasRelationship{};
            while (SUCCEEDED(enumerator->MoveNext(&hasRelationship)) && hasRelationship)
            {
                com_ptr<IOpcRelationship> relationship;
                check_hresult(enumerator->GetCurrent(relationship.put()));
                OPC_URI_TARGET_MODE mode{};
                check_hresult(relationship->GetTargetMode(&mode));
                if (mode != OPC_URI_TARGET_MODE_INTERNAL) continue;
                wil::unique_cotaskmem_string id;
                check_hresult(relationship->GetId(id.put()));
                com_ptr<IUri> target;
                check_hresult(relationship->GetTargetUri(target.put()));
                wil::unique_bstr targetText;
                check_hresult(target->GetRawUri(targetText.put()));
                targets[id.get()] = targetText.get();
            }
            std::vector<std::wstring> strings;
            if (sheet)
            {
                com_ptr<IOpcPartUri> uri;
                check_hresult(factory->CreatePartUri(L"/xl/sharedStrings.xml", uri.put()));
                BOOL exists{};
                check_hresult(parts->PartExists(uri.get(), &exists));
                if (exists)
                {
                    auto xml = readPart(L"/xl/sharedStrings.xml");
                    Walk(xml, [&](const XmlNode& n) { if (n.name == L"si") strings.push_back(TextRuns(n)); });
                }
            }
            Walk(main, [&](const XmlNode& n) {
                if (n.name != (sheet ? L"sheet" : L"sldId") || outputRemaining == 0) return;
                const auto target = targets.find(Attribute(n, L"relId"));
                if (target == targets.end()) return;
                // Resolve package-relative part names; these never become filesystem paths.
                com_ptr<IOpcPartUri> source;
                check_hresult(factory->CreatePartUri((base + (sheet ? L"workbook.xml" : L"presentation.xml")).c_str(), source.put()));
                com_ptr<IUri> relative;
                check_hresult(CreateUri(target->second.c_str(), Uri_CREATE_ALLOW_RELATIVE, 0, relative.put()));
                com_ptr<IOpcPartUri> resolved;
                check_hresult(source->CombinePartUri(relative.get(), resolved.put()));
                wil::unique_bstr resolvedName;
                check_hresult(resolved->GetRawUri(resolvedName.put()));
                auto xml = readPart(resolvedName.get());
                std::wstring text;
                bool truncated = false;
                if (!sheet) Paragraphs(xml, text);
                else Walk(xml, [&](const XmlNode& row) {
                    if (row.name != L"row") return;
                    unsigned long rowIndex = 0;
                    try { rowIndex = std::stoul(Attribute(row, L"r")); } catch (...) { return; }
                    if (rowIndex > 200 || text.size() >= TextLimit) { truncated = true; return; }
                    size_t previousColumn = 0;
                    for (const auto& cell : row.children)
                    {
                        if (cell->name != L"c") continue;
                        size_t column = 0;
                        for (const auto c : Attribute(*cell, L"r"))
                        {
                            if (c < L'A' || c > L'Z') break;
                            column = column * 26 + static_cast<size_t>(c - L'A' + 1);
                            if (column > 50) break;
                        }
                        if (column > 50) { truncated = true; continue; }
                        if (column == 0) column = previousColumn + 1;
                        while (++previousColumn < column) text += L"\t";
                        std::wstring value = TextRuns(*cell);
                        Walk(*cell, [&](const XmlNode& v) { if (v.name == L"v") value = v.text; });
                        if (Attribute(*cell, L"t") == L"s")
                        {
                            try { value = strings.at(std::stoul(value)); } catch (...) { value = L"[Invalid shared string]"; }
                        }
                        text += value.substr(0, TextLimit - std::min(TextLimit, text.size())) + L"\t";
                    }
                    text += L"\n";
                });
                if (truncated) text += L"\n[Preview limited to 200 rows and 50 columns]";
                add(sheet ? Attribute(n, L"name") : L"Slide " + std::to_wstring(sections.size() + 1), std::move(text));
            });
        }
        return sections;
    }
    // Decode a bounded file prefix. Trim only an incomplete trailing code point.
    inline std::optional<std::wstring> DecodeText(std::string bytes, bool truncated)
    {
        std::wstring text;
        if (bytes.size() >= 2 &&
            (bytes.substr(0, 2) == "\xff\xfe" || bytes.substr(0, 2) == "\xfe\xff"))
        {
            const bool little = static_cast<unsigned char>(bytes[0]) == 0xff;
            if (!truncated && bytes.size() % 2) return std::nullopt;
            for (size_t i = 2; i + 1 < bytes.size(); i += 2)
            {
                const auto a = static_cast<unsigned char>(bytes[i]);
                const auto b = static_cast<unsigned char>(bytes[i + 1]);
                text += static_cast<wchar_t>(little ? a | (b << 8) : (a << 8) | b);
            }
            if (truncated && !text.empty() && text.back() >= 0xd800 && text.back() <= 0xdbff) text.pop_back();
        }
        else
        {
            if (bytes.starts_with("\xef\xbb\xbf")) bytes.erase(0, 3);
            if (truncated && !bytes.empty())
            {
                auto start = bytes.size() - 1;
                while (start > 0 && (static_cast<unsigned char>(bytes[start]) & 0xc0) == 0x80) --start;
                const auto lead = static_cast<unsigned char>(bytes[start]);
                const size_t needed = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
                if (bytes.size() - start < needed) bytes.resize(start);
            }
            if (bytes.find('\0') != std::string::npos) return std::nullopt;
            const auto length = bytes.empty() ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
            if (!bytes.empty() && !length) return std::nullopt;
            text.resize(static_cast<size_t>(length));
            if (length) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
        }
        for (size_t i = 0; i < text.size(); ++i)
        {
            const auto c = text[i];
            if (c == 0 || (c < 32 && c != L'\t' && c != L'\n' && c != L'\r')) return std::nullopt;
            if (c >= 0xd800 && c <= 0xdbff)
            {
                if (++i >= text.size() || text[i] < 0xdc00 || text[i] > 0xdfff) return std::nullopt;
            }
            else if (c >= 0xdc00 && c <= 0xdfff) return std::nullopt;
        }
        if (truncated) text += L"\n[Preview truncated at 1 MiB]";
        return text;
    }

}
