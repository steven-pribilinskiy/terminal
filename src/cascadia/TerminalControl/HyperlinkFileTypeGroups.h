// File classification is generated from the shared Lintel catalog.
#pragma once
#include "../inc/LintelFileTypes.g.h"
namespace winrt::Microsoft::Terminal::Control::HyperlinkFileTypeGroups
{
    inline bool PathInGroup(HyperlinkFileTypeGroup group, const std::wstring_view& path)
    {
        const auto& type = Lintel::FindFileType(path);
        switch (group)
        {
        case HyperlinkFileTypeGroup::Image: return Lintel::Contains(type.groups, L"image");
        case HyperlinkFileTypeGroup::Video: return Lintel::Contains(type.groups, L"video");
        case HyperlinkFileTypeGroup::Audio: return Lintel::Contains(type.groups, L"audio");
        case HyperlinkFileTypeGroup::Media: return Lintel::Contains(type.groups, L"media");
        case HyperlinkFileTypeGroup::SourceCode: return Lintel::Contains(type.groups, L"sourceCode");
        case HyperlinkFileTypeGroup::Document: return Lintel::Contains(type.groups, L"document");
        case HyperlinkFileTypeGroup::Archive: return Lintel::Contains(type.groups, L"archive");
        case HyperlinkFileTypeGroup::Executable: return Lintel::Contains(type.groups, L"executable");
        default: return false;
        }
    }
}
