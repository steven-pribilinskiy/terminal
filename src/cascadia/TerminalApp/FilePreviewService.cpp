// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
#include "pch.h"
#include "HyperlinkPreviewService.h"
#include "FilePreviewReader.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <iomanip>
#include "../inc/LintelFileTypes.g.h"

namespace winrt::TerminalApp::implementation
{
    namespace Control = winrt::Microsoft::Terminal::Control;
    using namespace ::Microsoft::Terminal::FilePreview;

    Windows::Foundation::IAsyncOperation<Control::HyperlinkPreview> HyperlinkPreviewService::GetFilePreviewAsync(hstring resolvedFilePath)
    {
        const auto cancellation = co_await get_cancellation_token();
        co_await resume_background();
        if (cancellation()) throw hresult_canceled();
        check_hresult(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        const auto apartment = wil::scope_exit([] { CoUninitialize(); });
        Control::HyperlinkPreview preview;
        preview.IntegrationName(L"File");
        preview.IntegrationIcon(L"\uE8A5");
        preview.FilePath(resolvedFilePath);
        preview.SourceText(resolvedFilePath);
        try
        {
            const std::filesystem::path path{ resolvedFilePath.c_str() };
            if (!std::filesystem::is_regular_file(path)) throw hresult_error(E_FAIL, L"This path is not an accessible regular file.");
            const auto size = std::filesystem::file_size(path);
            auto fields = single_threaded_vector<Control::HyperlinkPreviewField>();
            Control::HyperlinkPreviewField title;
            title.Kind(Control::HyperlinkPreviewFieldKind::Title);
            title.Value(path.filename().wstring());
            fields.Append(title);
            Control::HyperlinkPreviewField metadata;
            const auto& fileType = Lintel::FindFileType(path.wstring());
            preview.IntegrationName(fileType.name);
            preview.IntegrationIcon(fileType.icon);
            std::wostringstream sizeLabel;
            if (size < 1024) sizeLabel << size << (size == 1 ? L" byte" : L" bytes");
            else if (size < 1024 * 1024) sizeLabel << std::fixed << std::setprecision(1) << static_cast<double>(size) / 1024 << L" KiB";
            else sizeLabel << std::fixed << std::setprecision(1) << static_cast<double>(size) / (1024 * 1024) << L" MiB";
            metadata.Value(std::wstring{ fileType.name } + L" · " + sizeLabel.str());
            metadata.IconUri(fileType.icon);
            fields.Append(metadata);
            preview.Fields(fields);
            std::wstring ext = path.extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (size > 50 * 1024 * 1024) throw hresult_error(E_FAIL, L"File exceeds the 50 MiB preview limit.");
            if (ext == L".pdf") preview.FileKind(L"pdf");
            else if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".gif" || ext == L".bmp" || ext == L".tif" || ext == L".tiff") preview.FileKind(L"image");
            else if (ext == L".docx" || ext == L".xlsx" || ext == L".pptx")
            {
                preview.FileKind(ext == L".xlsx" ? L"sheet" : L"document");
                auto sections = single_threaded_vector<Control::HyperlinkPreviewTab>();
                for (const auto& [label, body] : ReadOfficeSections(path.wstring(), ext))
                {
                    Control::HyperlinkPreviewTab section;
                    section.Label(label);
                    section.Body(body);
                    sections.Append(section);
                }
                preview.FileSections(sections);
            }
            else
            {
                std::ifstream file{ path, std::ios::binary };
                if (!file) throw hresult_error(E_ACCESSDENIED, L"File could not be opened for preview.");
                std::string bytes(std::min(static_cast<size_t>(size), TextLimit), '\0');
                file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                if (file.bad()) throw hresult_error(E_FAIL, L"File could not be read completely.");
                bytes.resize(static_cast<size_t>(file.gcount()));
                auto decoded = DecodeText(bytes, size > TextLimit);
                if (!decoded) throw hresult_error(E_FAIL, L"No built-in preview for this binary format or text encoding.");
                const auto& text = *decoded;
                Control::HyperlinkPreviewTab section;
                section.Label(L"Text");
                section.Body(text);
                preview.FileSections(single_threaded_vector<Control::HyperlinkPreviewTab>({ section }));
                preview.FileKind(fileType.language == L"markdown" ? L"markdown" : L"text");
            }
        }
        catch (const hresult_error& error) { preview.Error(error.message()); }
        catch (...) { preview.Error(L"File could not be read. It may be missing, inaccessible, or damaged."); }
        if (cancellation()) throw hresult_canceled();
        co_return preview;
    }
}
