// File classification is generated from the shared Lintel catalog.
#pragma once
#include "../inc/LintelFileTypes.g.h"
namespace winrt::Microsoft::Terminal::Control::HyperlinkFileTypeGroups
{
    inline bool ExtensionInGroup(HyperlinkFileTypeGroup group, const std::wstring_view& extension)
    {
        switch (group)
        {
        case HyperlinkFileTypeGroup::Image: return Lintel::GroupContains(L"image", extension);
        case HyperlinkFileTypeGroup::Video: return Lintel::GroupContains(L"video", extension);
        case HyperlinkFileTypeGroup::Audio: return Lintel::GroupContains(L"audio", extension);
        case HyperlinkFileTypeGroup::Media: return Lintel::GroupContains(L"media", extension);
        case HyperlinkFileTypeGroup::SourceCode: return Lintel::GroupContains(L"sourceCode", extension);
        case HyperlinkFileTypeGroup::Document: return Lintel::GroupContains(L"document", extension);
        case HyperlinkFileTypeGroup::Archive: return Lintel::GroupContains(L"archive", extension);
        case HyperlinkFileTypeGroup::Executable: return Lintel::GroupContains(L"executable", extension);
        default: return false;
        }
    }
}
