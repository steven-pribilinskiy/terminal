// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Reading the build that `tools\Deploy-TerminalSlots.ps1` has staged for the
// Dev slot, so a running Dev window can offer to promote it.
//
// The two slots exist because the Dev slot hosts live sessions and the Test
// slot does not. A deploy therefore registers only Test and leaves the Dev
// payload staged on disk: swapping it means closing whatever is running in it,
// and that is the user's decision to make, not the build's. This header is the
// read-only half of that handshake -- it answers "is there something waiting,
// and what is it", and nothing here promotes anything.

#pragma once

#include "BuildInfo.h"

#include <til/io.h>
#include <til/u8u16convert.h>
#include <winrt/Windows.Data.Json.h>

namespace TerminalApp::SlotPromotion
{
    // Hardcoded on purpose, and deliberately the same literal as
    // tools\Deploy-TerminalSlots.ps1 and tools\Register-DevSlot.ps1. The slot
    // root is a fixed local path rather than a setting so that a settings file
    // cannot redirect what gets promoted over the Terminal you are running in.
    inline constexpr std::wstring_view SlotRoot{ L"C:\\TerminalSlots" };
    inline constexpr std::wstring_view PendingMarkerName{ L"dev-pending.json" };

    inline std::filesystem::path PendingMarkerPath()
    {
        return std::filesystem::path{ SlotRoot } / PendingMarkerName;
    }

    struct StagedBuild
    {
        winrt::hstring Commit;
        winrt::hstring CommitFull;
        winrt::hstring Branch;
        bool Dirty{ false };
        int64_t Timestamp{ 0 };
        winrt::hstring Payload;

        std::wstring RelativeAge() const
        {
            return BuildInfo::RelativeAge(Timestamp);
        }

        // "a1b2c3d+dirty (main), 4 minutes ago"
        winrt::hstring Describe() const
        {
            return winrt::hstring{ fmt::format(FMT_COMPILE(L"{}{} ({}), {}"),
                                               std::wstring_view{ Commit },
                                               Dirty ? L"+dirty" : L"",
                                               std::wstring_view{ Branch },
                                               RelativeAge()) };
        }
    };

    // nullopt when nothing is staged, or when the marker is unreadable or
    // malformed. A missing marker is the normal state on a machine that has
    // never run the deploy script, so it is not worth logging.
    inline std::optional<StagedBuild> ReadStaged()
    try
    {
        const auto contents{ til::io::read_file_as_utf8_string_if_exists(PendingMarkerPath()) };
        if (contents.empty())
        {
            return std::nullopt;
        }

        winrt::Windows::Data::Json::JsonObject root{ nullptr };
        if (!winrt::Windows::Data::Json::JsonObject::TryParse(winrt::hstring{ til::u8u16(contents) }, root))
        {
            return std::nullopt;
        }

        StagedBuild staged;
        staged.Commit = root.GetNamedString(L"commit", L"");
        staged.CommitFull = root.GetNamedString(L"commitFull", L"");
        staged.Branch = root.GetNamedString(L"branch", L"");
        staged.Dirty = root.GetNamedNumber(L"dirty", 0) != 0;
        staged.Timestamp = static_cast<int64_t>(root.GetNamedNumber(L"timestamp", 0));
        staged.Payload = root.GetNamedString(L"payload", L"");

        // A marker that names nothing is not a promotion candidate.
        if (staged.Payload.empty() || staged.Timestamp == 0)
        {
            return std::nullopt;
        }

        return staged;
    }
    catch (...)
    {
        LOG_CAUGHT_EXCEPTION();
        return std::nullopt;
    }

    // Is the staged payload a different build from the one this process is
    // running?
    //
    // Commit alone is not enough: rebuilding the same commit with uncommitted
    // changes -- which is most of what a working session does -- produces a
    // genuinely different binary at the same commit hash. So the timestamp is
    // the tiebreak, and it is compared with > rather than != so that an older
    // marker left behind by a previous deploy never advertises itself as new.
    inline bool DiffersFromRunning(const StagedBuild& staged) noexcept
    {
        if (!staged.CommitFull.empty() && staged.CommitFull != winrt::hstring{ TERMINAL_BUILD_COMMIT_FULL })
        {
            return true;
        }
        return staged.Timestamp > static_cast<int64_t>(TERMINAL_BUILD_TIMESTAMP);
    }

    // Only the Dev slot can be promoted into, so only the Dev slot offers it.
    // The Test slot is replaced wholesale by the next deploy, and a real
    // install has nothing to do with any of this.
    inline bool ThisSlotCanBePromoted() noexcept
    {
        return BuildInfo::CurrentSlot() == BuildInfo::Slot::Dev;
    }

    // The staged build worth offering, or nullopt if there is nothing to offer.
    inline std::optional<StagedBuild> PendingPromotion()
    {
        if (!ThisSlotCanBePromoted())
        {
            return std::nullopt;
        }

        auto staged{ ReadStaged() };
        if (staged && DiffersFromRunning(*staged))
        {
            return staged;
        }
        return std::nullopt;
    }
}
