// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "InteractionViewModel.h"
#include "InteractionViewModel.g.cpp"
#include "EnumEntry.h"

using namespace winrt::Windows::UI::Xaml::Navigation;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::Terminal::Settings::Model;

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    InteractionViewModel::InteractionViewModel(Model::GlobalAppSettings globalSettings, Model::WindowSettings windowSettings) :
        _GlobalSettings{ globalSettings },
        _WindowSettings{ windowSettings }
    {
        INITIALIZE_BINDABLE_ENUM_SETTING(TabSwitcherMode, TabSwitcherMode, TabSwitcherMode, L"Globals_TabSwitcherMode", L"Content");
        INITIALIZE_BINDABLE_ENUM_SETTING(CopyFormat, CopyFormat, winrt::Microsoft::Terminal::Control::CopyFormat, L"Globals_CopyFormat", L"Content");
        INITIALIZE_BINDABLE_ENUM_SETTING(ConfirmOnClose, ConfirmOnClose, Model::ConfirmOnClose, L"Globals_ConfirmOnClose", L"Content");
    }

    winrt::hstring InteractionViewModel::SafeUriSchemes() const
    {
        const auto schemes = _WindowSettings.SafeUriSchemes();
        if (!schemes || schemes.Size() == 0)
        {
            return {};
        }

        std::wstring joined;
        for (const auto& scheme : schemes)
        {
            if (!joined.empty())
            {
                joined.append(L", ");
            }
            joined.append(scheme);
        }
        return winrt::hstring{ joined };
    }

    void InteractionViewModel::SafeUriSchemes(const winrt::hstring& value)
    {
        std::vector<winrt::hstring> schemes;
        for (const auto& part : til::split_iterator{ std::wstring_view{ value }, L',' })
        {
            if (const auto trimmed = til::trim(part, L' '); !trimmed.empty())
            {
                schemes.emplace_back(trimmed);
            }
        }

        // An empty box clears the setting rather than storing an empty list, so the JSON
        // goes back to not mentioning it at all and the default applies again.
        if (schemes.empty())
        {
            _WindowSettings.ClearSafeUriSchemes();
        }
        else
        {
            _WindowSettings.SafeUriSchemes(winrt::single_threaded_vector<winrt::hstring>(std::move(schemes)));
        }

        _NotifyChanges(L"SafeUriSchemes");
    }
}
