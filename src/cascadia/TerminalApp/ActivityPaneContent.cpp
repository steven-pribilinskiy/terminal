// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "ActivityPaneContent.h"
#include "ActivityPaneContent.g.cpp"
#include "ActivityEntry.g.cpp"
#include "Utils.h"

#include <ActivityLog.h>
#include <til/io.h>
#include <til/u8u16convert.h>
// Not in TerminalApp's pch -- SlotPromotion.h includes it explicitly too.
#include <winrt/Windows.Data.Json.h>

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

    static winrt::hstring _stringField(const Windows::Data::Json::JsonObject& obj, std::wstring_view name)
    {
        if (!obj.HasKey(name))
        {
            return {};
        }
        const auto value{ obj.GetNamedValue(name) };
        switch (value.ValueType())
        {
        case Windows::Data::Json::JsonValueType::String:
            return value.GetString();
        case Windows::Data::Json::JsonValueType::Number:
        {
            // pid and parentPid are numbers in the file but only ever displayed,
            // so they are carried as text. Printed as an integer rather than
            // GetNumber()'s double, which would render 4242 as "4242.000000".
            const auto number{ value.GetNumber() };
            return winrt::hstring{ fmt::format(L"{}", static_cast<int64_t>(number)) };
        }
        default:
            return {};
        }
    }

    winrt::com_ptr<ActivityEntry> ActivityEntry::FromJsonLine(std::string_view line)
    {
        // A line can be torn if a process died mid-append, so a parse failure is
        // expected rather than exceptional: skip it and keep reading.
        Windows::Data::Json::JsonObject obj{ nullptr };
        if (!Windows::Data::Json::JsonObject::TryParse(winrt::hstring{ til::u8u16(line) }, obj) || !obj)
        {
            return nullptr;
        }

        auto entry{ winrt::make_self<ActivityEntry>() };
        entry->_timestamp = _stringField(obj, L"ts");
        entry->_kind = _stringField(obj, L"kind");
        entry->_exe = _stringField(obj, L"exe");
        entry->_commandLine = _stringField(obj, L"commandLine");
        entry->_cwd = _stringField(obj, L"cwd");
        entry->_parentExe = _stringField(obj, L"parentExe");
        entry->_reason = _stringField(obj, L"reason");
        entry->_pid = _stringField(obj, L"pid");
        entry->_parentPid = _stringField(obj, L"parentPid");

        // The leaf of the image path, for the list's headline. Falls back to the
        // whole string when there is no separator, so a bare "wscript.exe" still
        // shows something.
        {
            const std::wstring_view exe{ entry->_exe };
            const auto slash{ exe.find_last_of(L"\\/") };
            entry->_exeName = winrt::hstring{ slash == std::wstring_view::npos ? exe : exe.substr(slash + 1) };
        }

        // One lowercased haystack so the filter box is a single substring test
        // over everything, rather than the caller having to guess which field a
        // remembered fragment was in.
        {
            std::wstring haystack;
            for (const auto& part : { entry->_timestamp, entry->_kind, entry->_exe, entry->_commandLine, entry->_cwd, entry->_parentExe, entry->_reason, entry->_pid, entry->_parentPid })
            {
                haystack.append(part);
                haystack.push_back(L' ');
            }
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](wchar_t c) { return til::tolower_ascii(c); });
            entry->_searchText = winrt::hstring{ haystack };
        }

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

        const auto path{ ::Microsoft::Terminal::ActivityLog::Path() };
        if (path.empty())
        {
            _setStatus(RS_(L"ActivityPaneNoLocation/Text"));
            _applyFilter();
            return;
        }

        // Read the rotated generation first so the combined view runs in order,
        // then let the cap drop the oldest.
        auto rotated{ path };
        rotated.replace_filename(L"activity.1.jsonl");

        std::string contents{ til::io::read_file_as_utf8_string_if_exists(rotated) };
        contents.append(til::io::read_file_as_utf8_string_if_exists(path));

        if (contents.empty())
        {
            _setStatus(::Microsoft::Terminal::ActivityLog::Enabled() ?
                           RS_(L"ActivityPaneEmpty/Text") :
                           RS_(L"ActivityPaneDisabled/Text"));
            _applyFilter();
            return;
        }

        std::vector<winrt::com_ptr<ActivityEntry>> parsed;
        size_t pos{ 0 };
        while (pos < contents.size())
        {
            auto end{ contents.find('\n', pos) };
            if (end == std::string::npos)
            {
                end = contents.size();
            }
            auto line{ std::string_view{ contents }.substr(pos, end - pos) };
            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }
            if (!line.empty())
            {
                if (auto entry{ ActivityEntry::FromJsonLine(line) })
                {
                    parsed.push_back(std::move(entry));
                }
            }
            pos = end + 1;
        }

        // Newest first: the reason you opened this is something that just
        // happened.
        _all.assign(parsed.rbegin(), parsed.rend());
        if (_all.size() > MaxEntries)
        {
            _all.resize(MaxEntries);
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
