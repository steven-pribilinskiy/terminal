// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "ActivityPaneContent.h"
#include "ActivityPaneContent.g.cpp"
#include "ActivityEntry.g.cpp"
#include "Utils.h"

// ActivityPaneContent.h pulls in ActivityLogReader.h, which brings the log's
// path, the parse, and the Windows.Data.Json include it needs (not in
// TerminalApp's pch -- SlotPromotion.h includes it explicitly too).
#include <ActivityLog.h>

using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::Terminal::Settings;
using namespace winrt::Microsoft::Terminal::Settings::Model;

namespace winrt
{
    namespace WUX = Windows::UI::Xaml;
    using IInspectable = Windows::Foundation::IInspectable;
}

namespace winrt::TerminalApp::implementation
{
    // The log can be long and the interesting entry is almost always recent, so
    // only the tail is read back. 4000 lines is far more than anyone scrolls and
    // still cheap to parse on the UI thread.
    static constexpr size_t MaxEntries{ 4000 };

    winrt::com_ptr<ActivityEntry> ActivityEntry::From(const ::Microsoft::Terminal::ActivityLog::ReadEntry& read)
    {
        auto entry{ winrt::make_self<ActivityEntry>() };
        entry->_timestamp = winrt::hstring{ read.timestamp };
        entry->_kind = winrt::hstring{ read.kind };
        entry->_exe = winrt::hstring{ read.exe };
        entry->_exeName = winrt::hstring{ read.exeName };
        entry->_commandLine = winrt::hstring{ read.commandLine };
        entry->_cwd = winrt::hstring{ read.cwd };
        entry->_parentExe = winrt::hstring{ read.parentExe };
        entry->_reason = winrt::hstring{ read.reason };
        entry->_pid = winrt::hstring{ read.pid };
        entry->_parentPid = winrt::hstring{ read.parentPid };
        entry->_searchText = winrt::hstring{ read.searchText };
        return entry;
    }

    ActivityPaneContent::ActivityPaneContent() :
        _entries{ winrt::single_threaded_observable_vector<TerminalApp::ActivityEntry>() }
    {
        InitializeComponent();

        WUX::Automation::AutomationProperties::SetName(*this, RS_(L"ActivityPaneTitle/Text"));

        // The log is read in UpdateSettings, not here. _reload raises
        // PropertyChanged, and doing that from a constructor means raising an
        // event on an object that is not finished being built -- SnippetsPaneContent
        // loads in UpdateSettings for the same reason, and _MakePane calls it
        // immediately after construction either way.
    }

    void ActivityPaneContent::_setStatus(winrt::hstring text)
    {
        _statusText = std::move(text);
        PropertyChanged.raise(*this, WUX::Data::PropertyChangedEventArgs{ L"StatusText" });
        PropertyChanged.raise(*this, WUX::Data::PropertyChangedEventArgs{ L"IsEmpty" });
    }

    void ActivityPaneContent::_reload()
    try
    {
        _all.clear();

        if (::Microsoft::Terminal::ActivityLog::Path().empty())
        {
            _setStatus(RS_(L"ActivityPaneNoLocation/Text"));
            _applyFilter();
            return;
        }

        // Newest first, already parsed. Shared with the Settings UI's Activity
        // page so the two viewers cannot disagree about the same file.
        const auto records{ ::Microsoft::Terminal::ActivityLog::ReadRecent(MaxEntries) };
        if (records.empty())
        {
            _setStatus(::Microsoft::Terminal::ActivityLog::Enabled() ?
                           RS_(L"ActivityPaneEmpty/Text") :
                           RS_(L"ActivityPaneDisabled/Text"));
            _applyFilter();
            return;
        }

        _all.reserve(records.size());
        for (const auto& record : records)
        {
            _all.push_back(ActivityEntry::From(record));
        }

        // fmt::runtime, because the format string comes from resources rather than
        // source -- without it this does not compile against a non-literal.
        _setStatus(winrt::hstring{ fmt::format(fmt::runtime(std::wstring_view{ RS_(L"ActivityPaneCount/Text") }), _all.size()) });
        _applyFilter();
    }
    catch (...)
    {
        LOG_CAUGHT_EXCEPTION();
        _setStatus(RS_(L"ActivityPaneReadFailed/Text"));
    }

    void ActivityPaneContent::_applyFilter()
    {
        std::wstring needle{ _filterBox().Text() };
        std::transform(needle.begin(), needle.end(), needle.begin(), [](wchar_t c) { return til::tolower_ascii(c); });

        _entries.Clear();
        for (const auto& entry : _all)
        {
            // Bind the hstring before making a view of it, rather than viewing a
            // temporary.
            const auto haystack{ entry->SearchText() };
            if (needle.empty() || std::wstring_view{ haystack }.find(needle) != std::wstring_view::npos)
            {
                _entries.Append(*entry);
            }
        }

        PropertyChanged.raise(*this, WUX::Data::PropertyChangedEventArgs{ L"IsEmpty" });
    }

    void ActivityPaneContent::_filterTextChanged(const IInspectable& /*sender*/, const WUX::RoutedEventArgs& /*args*/)
    {
        _applyFilter();
    }

    void ActivityPaneContent::_refreshClick(const IInspectable& /*sender*/, const WUX::RoutedEventArgs& /*args*/)
    {
        _reload();
    }

    void ActivityPaneContent::_closePaneClick(const IInspectable& /*sender*/, const WUX::RoutedEventArgs& /*args*/)
    {
        Close();
    }

    void ActivityPaneContent::UpdateSettings(const CascadiaSettings& settings,
                                            const WindowSettings& windowSettings)
    {
        _settings = settings;
        _windowSettings = windowSettings;

        // Also the first load: _MakePane calls this straight after constructing us.
        // Re-reading on a settings change is right too -- turning the log on is the
        // likeliest reason to come back to this pane.
        _reload();
    }

    winrt::WUX::FrameworkElement ActivityPaneContent::GetRoot()
    {
        return *this;
    }

    winrt::Windows::Foundation::Size ActivityPaneContent::MinimumSize()
    {
        return { 320, 200 };
    }

    void ActivityPaneContent::Focus(winrt::WUX::FocusState reason)
    {
        _filterBox().Focus(reason);
    }

    void ActivityPaneContent::Close()
    {
        CloseRequested.raise(*this, nullptr);
    }

    INewContentArgs ActivityPaneContent::GetNewTerminalArgs(BuildStartupKind /*kind*/) const
    {
        return BaseContentArgs(L"activity");
    }

    winrt::hstring ActivityPaneContent::Icon() const
    {
        static constexpr std::wstring_view glyph{ L"\xE81C" }; // History
        return winrt::hstring{ glyph };
    }

    winrt::WUX::Media::Brush ActivityPaneContent::BackgroundBrush()
    {
        static const auto key = winrt::box_value(L"SettingsUiTabBrush");
        return ThemeLookup(WUX::Application::Current().Resources(),
                           _settings.GlobalSettings().CurrentTheme(_windowSettings).RequestedTheme(),
                           key)
            .try_as<winrt::WUX::Media::Brush>();
    }
}
