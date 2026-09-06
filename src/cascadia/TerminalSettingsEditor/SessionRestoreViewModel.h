// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "SessionRestoreViewModel.g.h"
#include "ViewModelHelpers.h"
#include "Utils.h"
#include <cppwinrt_utils.h>

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    struct SessionRestoreViewModel : SessionRestoreViewModelT<SessionRestoreViewModel>, ViewModelHelper<SessionRestoreViewModel>
    {
    public:
        SessionRestoreViewModel(Model::CascadiaSettings settings);

        using ViewModelHelper<SessionRestoreViewModel>::PropertyChanged;

        // Hand-written rather than GETSET_BINDABLE_ENUM_SETTING, for the one
        // reason that macro cannot cover: its setter announces nothing, and on
        // this page two whole groups grey themselves out from this value. The
        // getter is the macro's, HasKey guard and all -- see the comment on the
        // macro for why that guard is load-bearing.
        Windows::Foundation::Collections::IObservableVector<Editor::EnumEntry> FirstWindowPreferenceList() const noexcept { return _FirstWindowPreferenceList; }
        Windows::Foundation::IInspectable CurrentFirstWindowPreference();
        void CurrentFirstWindowPreference(const Windows::Foundation::IInspectable& enumEntry);
        bool RestoreEnabled() const;
        bool ContentEnabled() const;
        bool ResumeNotificationEnabled();
        bool PersistIntervalEnabled() const;

        GETSET_BINDABLE_ENUM_SETTING(ResumeSessionNotification, Model::ResumeSessionNotification, _Settings.GlobalSettings().ResumeSessionNotification);

        PERMANENT_OBSERVABLE_PROJECTED_SETTING(_Settings.GlobalSettings(), ResumeAgents);
        PERMANENT_OBSERVABLE_PROJECTED_SETTING(_Settings.GlobalSettings(), ResumeMultiplexers);
        PERMANENT_OBSERVABLE_PROJECTED_SETTING(_Settings.GlobalSettings(), PersistBufferPeriodically);
        PERMANENT_OBSERVABLE_PROJECTED_SETTING(_Settings.GlobalSettings(), BufferPersistIntervalMinutes);

        // The two program lists are edited as one comma-separated line each.
        // A settings page has no list editor to reuse, and a program name has
        // no commas in it, so the round trip is lossless.
        winrt::hstring ResumeExtraProgramsText();
        void ResumeExtraProgramsText(const winrt::hstring& value);
        winrt::hstring ResumeExcludedProgramsText();
        void ResumeExcludedProgramsText(const winrt::hstring& value);
        bool AnyResumeEnabled();

        // What the last seven days of persistence passes cost, drawn with
        // block characters rather than a chart: it needs no XAML shapes, no
        // converters, and follows the theme for free.
        winrt::hstring PersistCostSparkline();
        winrt::hstring PersistCostSummary();

    private:
        Model::CascadiaSettings _Settings;

        Windows::Foundation::Collections::IObservableVector<Editor::EnumEntry> _FirstWindowPreferenceList;
        Windows::Foundation::Collections::IMap<Model::FirstWindowPreference, Editor::EnumEntry> _FirstWindowPreferenceMap;
    };
}

namespace winrt::Microsoft::Terminal::Settings::Editor::factory_implementation
{
    BASIC_FACTORY(SessionRestoreViewModel);
}
