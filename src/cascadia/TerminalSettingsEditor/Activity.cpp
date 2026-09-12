// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "Activity.h"
#include "Activity.g.cpp"
#include "ActivityViewModel.g.cpp"
#include "ActivityEntryViewModel.g.cpp"

using namespace winrt::Windows::UI::Xaml::Navigation;
using namespace winrt::Microsoft::Terminal::Settings::Model;

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    // Deliberately far smaller than the pane's 4000. The list here is an
    // ItemsControl in the page's own scroll flow rather than a ListView with a
    // viewport of its own, so every row it is given is realized. 200 is more
    // than fits on a screen, the filter narrows it, and the pane is still there
    // for a full trawl.
    static constexpr size_t MaxEntries{ 200 };

    winrt::com_ptr<ActivityEntryViewModel> ActivityEntryViewModel::From(const ::Microsoft::Terminal::ActivityLog::ReadEntry& read)
    {
        auto entry{ winrt::make_self<ActivityEntryViewModel>() };
        entry->_timestamp = winrt::hstring{ read.timestamp };
        entry->_kind = winrt::hstring{ read.kind };
        entry->_exe = winrt::hstring{ read.exe };
        entry->_exeName = winrt::hstring{ read.exeName };
        entry->_commandLine = winrt::hstring{ read.commandLine };
        entry->_cwd = winrt::hstring{ read.cwd };
        entry->_parentExe = winrt::hstring{ read.parentExe };
        entry->_reason = winrt::hstring{ read.reason };
        return entry;
    }

    ActivityViewModel::ActivityViewModel(Model::CascadiaSettings settings) :
        _settings{ settings },
        _entries{ winrt::single_threaded_observable_vector<Editor::ActivityEntryViewModel>() }
    {
        Reload();
    }

    void ActivityViewModel::Reload()
    {
        const auto path{ ::Microsoft::Terminal::ActivityLog::Path() };
        _logPath = winrt::hstring{ path.wstring() };

        _all = ::Microsoft::Terminal::ActivityLog::ReadRecent(MaxEntries);
        _applyFilter();

        _NotifyChanges(L"LogPath");
    }

    void ActivityViewModel::Filter(const winrt::hstring& value)
    {
        if (_filter == value)
        {
            return;
        }
        _filter = value;
        _NotifyChanges(L"Filter");
        _applyFilter();
    }

    void ActivityViewModel::_applyFilter()
    {
        std::wstring needle{ _filter };
        std::transform(needle.begin(), needle.end(), needle.begin(), [](wchar_t c) { return til::tolower_ascii(c); });

        _entries.Clear();
        for (const auto& record : _all)
        {
            if (needle.empty() || record.searchText.find(needle) != std::wstring::npos)
            {
                _entries.Append(*ActivityEntryViewModel::From(record));
            }
        }

        if (_logPath.empty())
        {
            _statusText = RS_(L"Activity_NoLocation/Text");
        }
        else if (_all.empty())
        {
            // An empty file and a switched-off log look the same from here, and
            // they call for opposite responses, so say which one it is.
            _statusText = ::Microsoft::Terminal::ActivityLog::Enabled() ?
                              RS_(L"Activity_Empty/Text") :
                              RS_(L"Activity_Disabled/Text");
        }
        else
        {
            _statusText = winrt::hstring{ RS_fmt(L"Activity_Count/Text", _entries.Size(), _all.size()) };
        }

        _NotifyChanges(L"Entries");
        _NotifyChanges(L"StatusText");
        _NotifyChanges(L"HasEntries");
    }

    Activity::Activity()
    {
        InitializeComponent();
    }

    void Activity::OnNavigatedTo(const NavigationEventArgs& e)
    {
        const auto args = e.Parameter().as<Editor::NavigateToPageArgs>();
        _ViewModel = args.ViewModel().as<Editor::ActivityViewModel>();
        BringIntoViewWhenLoaded(args.ElementToFocus());

        TraceLoggingWrite(
            g_hTerminalSettingsEditorProvider,
            "NavigatedToPage",
            TraceLoggingDescription("Event emitted when the user navigates to a page in the settings UI"),
            TraceLoggingValue("activity", "PageId", "The identifier of the page that was navigated to"),
            TraceLoggingKeyword(MICROSOFT_KEYWORD_MEASURES),
            TelemetryPrivacyDataTag(PDT_ProductAndServiceUsage));
    }

    void Activity::RefreshButton_Click(const Windows::Foundation::IInspectable& /*sender*/, const Windows::UI::Xaml::RoutedEventArgs& /*e*/)
    {
        if (_ViewModel)
        {
            _ViewModel.Reload();
        }
    }
}
