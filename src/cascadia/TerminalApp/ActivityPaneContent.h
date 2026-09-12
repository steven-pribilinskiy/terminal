// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// A pane that reads activity.jsonl back. The log is only half the feature: a
// file full of JSON lines answers "where did that window come from" only if you
// already know to go looking for it and how to read it, and the moment you want
// that answer is the moment a window has just surprised you.
//
// Modelled on SnippetsPaneContent, which is the existing pane that shows a
// filterable list of structured records.

#pragma once

#include "ActivityPaneContent.g.h"
#include "ActivityEntry.g.h"
#include "BasicPaneEvents.h"

#include "../inc/ActivityLogReader.h"

namespace winrt::TerminalApp::implementation
{
    struct ActivityEntry : ActivityEntryT<ActivityEntry>
    {
        ActivityEntry() = default;

        winrt::hstring Timestamp() const noexcept { return _timestamp; }
        winrt::hstring Kind() const noexcept { return _kind; }
        winrt::hstring Exe() const noexcept { return _exe; }
        winrt::hstring ExeName() const noexcept { return _exeName; }
        winrt::hstring CommandLine() const noexcept { return _commandLine; }
        winrt::hstring WorkingDirectory() const noexcept { return _cwd; }
        winrt::hstring ParentExe() const noexcept { return _parentExe; }
        winrt::hstring Reason() const noexcept { return _reason; }
        winrt::hstring Pid() const noexcept { return _pid; }
        winrt::hstring ParentPid() const noexcept { return _parentPid; }
        winrt::hstring SearchText() const noexcept { return _searchText; }

        bool HasCommandLine() const noexcept { return !_commandLine.empty(); }
        bool HasWorkingDirectory() const noexcept { return !_cwd.empty(); }
        bool HasParent() const noexcept { return !_parentExe.empty() || !_parentPid.empty(); }

        // Wraps one record from ActivityLogReader.h in the projected type XAML
        // binds to. The parse itself is shared with the Settings UI's Activity
        // page, so the two viewers cannot read the same file differently.
        static winrt::com_ptr<ActivityEntry> From(const ::Microsoft::Terminal::ActivityLog::ReadEntry& read);

    private:
        winrt::hstring _timestamp;
        winrt::hstring _kind;
        winrt::hstring _exe;
        winrt::hstring _exeName;
        winrt::hstring _commandLine;
        winrt::hstring _cwd;
        winrt::hstring _parentExe;
        winrt::hstring _reason;
        winrt::hstring _pid;
        winrt::hstring _parentPid;
        winrt::hstring _searchText;
    };

    struct ActivityPaneContent : ActivityPaneContentT<ActivityPaneContent>, BasicPaneEvents
    {
        ActivityPaneContent();

        winrt::Windows::UI::Xaml::FrameworkElement GetRoot();

        void UpdateSettings(const winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings& settings,
                            const winrt::Microsoft::Terminal::Settings::Model::WindowSettings& windowSettings);

        winrt::Windows::Foundation::Size MinimumSize();
        void Focus(winrt::Windows::UI::Xaml::FocusState reason = winrt::Windows::UI::Xaml::FocusState::Programmatic);
        void Close();
        winrt::Microsoft::Terminal::Settings::Model::INewContentArgs GetNewTerminalArgs(BuildStartupKind kind) const;

        winrt::hstring Title() { return RS_(L"ActivityPaneTitle/Text"); }
        uint64_t TaskbarState() { return 0; }
        uint64_t TaskbarProgress() { return 0; }
        bool ReadOnly() { return true; }
        winrt::hstring Icon() const;
        Windows::Foundation::IReference<winrt::Windows::UI::Color> TabColor() const noexcept { return nullptr; }
        winrt::Windows::UI::Xaml::Media::Brush BackgroundBrush();

        winrt::Windows::Foundation::Collections::IObservableVector<TerminalApp::ActivityEntry> Entries() const noexcept { return _entries; }
        winrt::hstring StatusText() const noexcept { return _statusText; }
        bool IsEmpty() const noexcept { return _entries.Size() == 0; }

        til::property_changed_event PropertyChanged;

    private:
        friend struct ActivityPaneContentT<ActivityPaneContent>; // for Xaml to bind events

        winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings _settings{ nullptr };
        winrt::Microsoft::Terminal::Settings::Model::WindowSettings _windowSettings{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<TerminalApp::ActivityEntry> _entries{ nullptr };
        std::vector<winrt::com_ptr<ActivityEntry>> _all;
        winrt::hstring _statusText;

        void _reload();
        void _applyFilter();
        void _setStatus(winrt::hstring text);

        void _refreshClick(const Windows::Foundation::IInspectable& sender, const Windows::UI::Xaml::RoutedEventArgs& args);
        void _closePaneClick(const Windows::Foundation::IInspectable& sender, const Windows::UI::Xaml::RoutedEventArgs& args);
        void _filterTextChanged(const Windows::Foundation::IInspectable& sender, const Windows::UI::Xaml::RoutedEventArgs& args);
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    // No factory for ActivityEntry: its runtimeclass declares no constructor, so
    // nothing activates it from outside -- the pane builds them with make_self.
    // Same as FilteredTask next door, which also gets none.
    BASIC_FACTORY(ActivityPaneContent);
}
