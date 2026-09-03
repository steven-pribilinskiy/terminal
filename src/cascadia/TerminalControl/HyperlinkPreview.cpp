// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "HyperlinkPreview.h"

#include <winrt/Windows.UI.Xaml.Media.Imaging.h>

#include "HyperlinkPreviewField.g.cpp"
#include "HyperlinkPreview.g.cpp"
#include "HyperlinkPreviewHelpers.g.cpp"

using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Media;

namespace winrt::Microsoft::Terminal::Control::implementation
{
    // Badge backgrounds are translucent so the same brush reads on both the
    // light and the dark card background; the text keeps the card's foreground.
    Brush HyperlinkPreviewHelpers::BadgeBrush(const hstring& color)
    {
        auto pick = [](uint8_t r, uint8_t g, uint8_t b) {
            return SolidColorBrush{ Color{ 0x55, r, g, b } };
        };

        std::wstring lowered{ color };
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::towlower);

        if (lowered.size() == 7 && lowered.front() == L'#')
        {
            try
            {
                const auto value = std::stoul(lowered.substr(1), nullptr, 16);
                return pick(static_cast<uint8_t>((value >> 16) & 0xff),
                            static_cast<uint8_t>((value >> 8) & 0xff),
                            static_cast<uint8_t>(value & 0xff));
            }
            catch (...)
            {
            }
        }
        // Jira's statusCategory.colorName values, plus a few obvious ones.
        if (lowered == L"green" || lowered == L"success" || lowered == L"done" || lowered == L"live" || lowered == L"running" || lowered == L"active")
        {
            return pick(0x2e, 0xa0, 0x43);
        }
        if (lowered == L"yellow" || lowered == L"inprogress" || lowered == L"in progress" || lowered == L"warning" || lowered == L"waiting" || lowered == L"idle")
        {
            return pick(0xe0, 0xa8, 0x00);
        }
        if (lowered == L"red" || lowered == L"error" || lowered == L"failed" || lowered == L"blocked" || lowered == L"dead" || lowered == L"stale")
        {
            return pick(0xd0, 0x3c, 0x3c);
        }
        if (lowered == L"blue" || lowered == L"info" || lowered == L"new" || lowered == L"open")
        {
            return pick(0x3b, 0x82, 0xd6);
        }
        // "blue-gray" (Jira's To Do), "gray", "unknown", "exited", …
        return pick(0x80, 0x88, 0x90);
    }

    ImageSource HyperlinkPreviewHelpers::ImageFromUri(const hstring& uri)
    {
        if (uri.empty())
        {
            return nullptr;
        }
        try
        {
            Imaging::BitmapImage image;
            image.DecodePixelHeight(24);
            image.UriSource(Windows::Foundation::Uri{ uri });
            return image;
        }
        catch (...)
        {
            return nullptr;
        }
    }
}
