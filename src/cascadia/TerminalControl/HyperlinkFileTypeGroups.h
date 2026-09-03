/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- HyperlinkFileTypeGroups.h

Abstract:
- The extension lists behind each Control::HyperlinkFileTypeGroup, used when
  evaluating a HyperlinkTooltipRule's file-type match criteria against a
  hovered file:// (or bare POSIX path) link.

--*/
#pragma once

// Relies on the includer having already brought in the Microsoft.Terminal.Control
// WinRT projection (via pch.h) for the HyperlinkFileTypeGroup enum -- this header is
// only ever included from TermControl.cpp, after pch.h.
namespace winrt::Microsoft::Terminal::Control::HyperlinkFileTypeGroups
{
    inline constexpr std::wstring_view ImageExtensions[]{ L"png", L"jpg", L"jpeg", L"gif", L"bmp", L"webp", L"svg", L"ico" };
    inline constexpr std::wstring_view VideoExtensions[]{ L"mp4", L"mkv", L"webm", L"mov", L"avi" };
    inline constexpr std::wstring_view AudioExtensions[]{ L"mp3", L"wav", L"flac", L"ogg", L"m4a" };
    inline constexpr std::wstring_view SourceCodeExtensions[]{ L"cs", L"cpp", L"h", L"hpp", L"c", L"py", L"js", L"ts", L"rs", L"go", L"java", L"rb", L"ps1", L"sh" };
    inline constexpr std::wstring_view DocumentExtensions[]{ L"pdf", L"docx", L"xlsx", L"pptx", L"txt", L"md" };
    inline constexpr std::wstring_view ArchiveExtensions[]{ L"zip", L"7z", L"rar", L"tar", L"gz" };
    inline constexpr std::wstring_view ExecutableExtensions[]{ L"exe", L"msi", L"bat", L"cmd", L"ps1" };

    inline bool _extensionInList(const std::wstring_view& extension, const std::wstring_view* list, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            if (til::equals_insensitive_ascii(extension, list[i]))
            {
                return true;
            }
        }
        return false;
    }

    // extension must already be lowercased and contain no leading dot.
    inline bool ExtensionInGroup(HyperlinkFileTypeGroup group, const std::wstring_view& extension)
    {
        switch (group)
        {
        case HyperlinkFileTypeGroup::Image:
            return _extensionInList(extension, ImageExtensions, std::size(ImageExtensions));
        case HyperlinkFileTypeGroup::Video:
            return _extensionInList(extension, VideoExtensions, std::size(VideoExtensions));
        case HyperlinkFileTypeGroup::Audio:
            return _extensionInList(extension, AudioExtensions, std::size(AudioExtensions));
        case HyperlinkFileTypeGroup::Media:
            return _extensionInList(extension, ImageExtensions, std::size(ImageExtensions)) ||
                   _extensionInList(extension, VideoExtensions, std::size(VideoExtensions)) ||
                   _extensionInList(extension, AudioExtensions, std::size(AudioExtensions));
        case HyperlinkFileTypeGroup::SourceCode:
            return _extensionInList(extension, SourceCodeExtensions, std::size(SourceCodeExtensions));
        case HyperlinkFileTypeGroup::Document:
            return _extensionInList(extension, DocumentExtensions, std::size(DocumentExtensions));
        case HyperlinkFileTypeGroup::Archive:
            return _extensionInList(extension, ArchiveExtensions, std::size(ArchiveExtensions));
        case HyperlinkFileTypeGroup::Executable:
            return _extensionInList(extension, ExecutableExtensions, std::size(ExecutableExtensions));
        case HyperlinkFileTypeGroup::None:
        default:
            return false;
        }
    }
}
