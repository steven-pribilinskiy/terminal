// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "SessionRestoreViewModel.h"
#include "SessionRestoreViewModel.g.cpp"
#include "EnumEntry.h"

#include <til/io.h>
#include <til/string.h>
#include <til/u8u16convert.h>
#include <winrt/Windows.Data.Json.h>

using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::Terminal::Settings::Model;
using namespace winrt::Windows::UI::Xaml::Data;

namespace winrt::Microsoft::Terminal::Settings::Editor::implementation
{
    SessionRestoreViewModel::SessionRestoreViewModel(Model::CascadiaSettings settings) :
        _Settings{ settings }
    {
        INITIALIZE_BINDABLE_ENUM_SETTING(FirstWindowPreference, FirstWindowPreference, FirstWindowPreference, L"Globals_FirstWindowPreference", L"Content");
        INITIALIZE_BINDABLE_ENUM_SETTING(ResumeSessionNotification, ResumeSessionNotification, ResumeSessionNotification, L"Globals_ResumeSessionNotification", L"Content");

        PropertyChanged([this](auto&&, const PropertyChangedEventArgs& args) {
            const auto viewModelProperty{ args.PropertyName() };
            if (viewModelProperty == L"ResumeAgents" || viewModelProperty == L"ResumeMultiplexers")
            {
                // The notification setting is only reachable while something
                // is eligible to be resumed at all.
                _NotifyChanges(L"AnyResumeEnabled");
            }
        });
    }

    IInspectable SessionRestoreViewModel::CurrentFirstWindowPreference()
    {
        const auto key = _Settings.GlobalSettings().FirstWindowPreference();
        if (!_FirstWindowPreferenceMap || !_FirstWindowPreferenceMap.HasKey(key))
        {
            return nullptr;
        }
        return winrt::box_value<Editor::EnumEntry>(_FirstWindowPreferenceMap.Lookup(key));
    }

    // Not a plain projected setter: this one value decides whether either group
    // below it is live, so the two flags have to be announced with it. Note it
    // deliberately does NOT announce CurrentFirstWindowPreference -- the binding
    // called us because the selection already changed, and telling it otherwise
    // is how this page's neighbours have twice recursed until the stack was gone.
    void SessionRestoreViewModel::CurrentFirstWindowPreference(const IInspectable& enumEntry)
    {
        const auto ee = enumEntry.try_as<Editor::EnumEntry>();
        if (!ee)
        {
            return;
        }
        const auto setting = winrt::unbox_value<Model::FirstWindowPreference>(ee.EnumValue());
        if (_Settings.GlobalSettings().FirstWindowPreference() == setting)
        {
            return;
        }
        _Settings.GlobalSettings().FirstWindowPreference(setting);
        _NotifyChanges(L"RestoreEnabled", L"ContentEnabled");
    }

    // The same test the app makes: GlobalAppSettings::ShouldUsePersistedLayout is
    // this expression, and WindowEmperor gates capturing resume commands on it.
    bool SessionRestoreViewModel::RestoreEnabled() const
    {
        return _Settings.GlobalSettings().FirstWindowPreference() != Model::FirstWindowPreference::DefaultProfile;
    }

    // Saving pane contents needs the stricter of the two: WindowEmperor writes a
    // buffer only when the preference is PersistedLayoutAndContent, so the saving
    // settings say nothing at all under plain "Restore window layout". That used
    // to be documentation on one description string and nothing else, which is why
    // "Save pane contents periodically" shipped reading On while doing nothing.
    bool SessionRestoreViewModel::ContentEnabled() const
    {
        return _Settings.GlobalSettings().FirstWindowPreference() == Model::FirstWindowPreference::PersistedLayoutAndContent;
    }

    static winrt::hstring _joinPrograms(const Windows::Foundation::Collections::IVector<winrt::hstring>& list)
    {
        if (!list)
        {
            return {};
        }
        std::wstring joined;
        for (const auto& entry : list)
        {
            if (!joined.empty())
            {
                joined.append(L", ");
            }
            joined.append(entry);
        }
        return winrt::hstring{ joined };
    }

    static Windows::Foundation::Collections::IVector<winrt::hstring> _splitPrograms(std::wstring_view text)
    {
        std::vector<winrt::hstring> entries;
        for (const auto& part : til::split_iterator{ text, L',' })
        {
            const auto trimmed = til::trim(part, L' ');
            if (!trimmed.empty())
            {
                entries.emplace_back(trimmed);
            }
        }
        // An empty list is stored as nothing at all, so the key drops out of
        // settings.json instead of persisting as a confusing empty array.
        if (entries.empty())
        {
            return nullptr;
        }
        return winrt::single_threaded_vector<winrt::hstring>(std::move(entries));
    }

    winrt::hstring SessionRestoreViewModel::ResumeExtraProgramsText()
    {
        return _joinPrograms(_Settings.GlobalSettings().ResumeExtraPrograms());
    }

    void SessionRestoreViewModel::ResumeExtraProgramsText(const winrt::hstring& value)
    {
        if (ResumeExtraProgramsText() == value)
        {
            return;
        }
        _Settings.GlobalSettings().ResumeExtraPrograms(_splitPrograms(value));
        _NotifyChanges(L"ResumeExtraProgramsText", L"AnyResumeEnabled");
    }

    winrt::hstring SessionRestoreViewModel::ResumeExcludedProgramsText()
    {
        return _joinPrograms(_Settings.GlobalSettings().ResumeExcludedPrograms());
    }

    void SessionRestoreViewModel::ResumeExcludedProgramsText(const winrt::hstring& value)
    {
        if (ResumeExcludedProgramsText() == value)
        {
            return;
        }
        _Settings.GlobalSettings().ResumeExcludedPrograms(_splitPrograms(value));
        _NotifyChanges(L"ResumeExcludedProgramsText");
    }

    bool SessionRestoreViewModel::AnyResumeEnabled()
    {
        const auto globals = _Settings.GlobalSettings();
        if (globals.ResumeAgents() || globals.ResumeMultiplexers())
        {
            return true;
        }
        const auto extra = globals.ResumeExtraPrograms();
        return extra && extra.Size() > 0;
    }

    // Written by WindowEmperor after each periodic persistence pass. Read here
    // rather than plumbed through settings, because it is a log of what
    // happened and not a setting: nothing edits it, and it must survive being
    // absent, truncated or hand-deleted without complaint.
    struct PersistSample
    {
        int64_t Micros;
        int64_t Bytes;
    };

    static std::vector<PersistSample> _readPersistCostLog()
    {
        std::vector<PersistSample> samples;
        try
        {
            const std::filesystem::path path{ std::filesystem::path{ std::wstring_view{ Model::CascadiaSettings::SettingsDirectory() } } / L"persistence-timings.json" };
            const auto contents = til::io::read_file_as_utf8_string_if_exists(path);
            if (contents.empty())
            {
                return samples;
            }

            winrt::Windows::Data::Json::JsonArray parsed{ nullptr };
            if (!winrt::Windows::Data::Json::JsonArray::TryParse(winrt::hstring{ til::u8u16(contents) }, parsed))
            {
                return samples;
            }
            for (const auto& item : parsed)
            {
                if (item.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object)
                {
                    continue;
                }
                const auto obj = item.GetObject();
                samples.push_back(PersistSample{
                    static_cast<int64_t>(obj.GetNamedNumber(L"us", 0)),
                    static_cast<int64_t>(obj.GetNamedNumber(L"bytes", 0)) });
            }
        }
        CATCH_LOG();
        return samples;
    }

    winrt::hstring SessionRestoreViewModel::PersistCostSparkline()
    {
        const auto samples = _readPersistCostLog();
        if (samples.empty())
        {
            return {};
        }

        static constexpr std::wstring_view blocks{ L"▁▂▃▄▅▆▇█" };
        static constexpr size_t maxPoints = 48;
        const auto first = samples.size() > maxPoints ? samples.end() - maxPoints : samples.begin();

        // Scaled against the worst sample in view, so the shape shows how this
        // run compares with the rest of the week rather than against an
        // absolute ceiling nothing ever reaches.
        int64_t peak = 1;
        for (auto it = first; it != samples.end(); ++it)
        {
            peak = std::max(peak, it->Micros);
        }

        std::wstring line;
        line.reserve(maxPoints);
        for (auto it = first; it != samples.end(); ++it)
        {
            const auto level = static_cast<size_t>((it->Micros * 7) / peak);
            line.push_back(blocks[std::min<size_t>(level, 7)]);
        }
        return winrt::hstring{ line };
    }

    winrt::hstring SessionRestoreViewModel::PersistCostSummary()
    {
        auto samples = _readPersistCostLog();
        if (samples.empty())
        {
            return RS_(L"Globals_PersistCostNoData");
        }

        std::vector<int64_t> durations;
        durations.reserve(samples.size());
        for (const auto& sample : samples)
        {
            durations.push_back(sample.Micros);
        }
        std::sort(durations.begin(), durations.end());

        const auto median = durations[durations.size() / 2];
        const auto worst = durations.back();
        const auto latestBytes = samples.back().Bytes;

        // Milliseconds with a decimal, because a handful of small panes really
        // does serialize in well under one and rounding that to "0 ms" would
        // read as a broken counter rather than a fast one.
        return winrt::hstring{ fmt::format(
            FMT_COMPILE(L"{} passes · median {:.1f} ms · worst {:.1f} ms · {:.1f} MB on disk"),
            samples.size(),
            static_cast<double>(median) / 1000.0,
            static_cast<double>(worst) / 1000.0,
            static_cast<double>(latestBytes) / (1024.0 * 1024.0)) };
    }
}
