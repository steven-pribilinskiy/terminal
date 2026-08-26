// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Reading the build that has been staged for the Dev slot, so a running Dev
// window can offer to promote it.
//
// The two slots exist because the Dev slot hosts live sessions and the Test
// slot does not. A deploy therefore registers only Test and leaves the Dev
// payload staged on disk: swapping it means closing whatever is running in it,
// and that is the user's decision to make, not the build's. This header is the
// read-only half of that handshake -- it answers "is there something waiting,
// and what is it", and nothing here promotes anything.
//
// There is more than one thing that can stage a build. `tools\Deploy-Terminal
// Slots.ps1` does it after a local compile; `tools\Fetch-CIBuild.ps1` does it
// after downloading one that CI built. They are used on different machines and
// neither knows about the other, so each writes its OWN marker and this header
// picks the newest -- rather than both writing one file and racing over it.

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

    // Every marker starts with this and ends in .json: `dev-pending.json` from a
    // local deploy, `dev-pending-ci.json` from a fetched CI build. Matching a
    // prefix rather than naming each one means a new producer needs no change
    // here, and the file the old single-marker code wrote still matches.
    inline constexpr std::wstring_view PendingMarkerPrefix{ L"dev-pending" };
    inline constexpr std::wstring_view PendingMarkerExtension{ L".json" };

    // Shared by the reader below and by the folder watcher that decides a marker
    // has appeared (AppLogic::_StartWatchingSlotRoot). If those two disagreed
    // about what counts, a marker would be promotable but never noticed -- the
    // button would only appear the next time something else refreshed it.
    inline bool IsPendingMarkerName(const std::filesystem::path& filename)
    {
        return filename.native().starts_with(PendingMarkerPrefix) &&
               filename.extension() == PendingMarkerExtension;
    }

    struct StagedBuild
    {
        winrt::hstring Commit;
        winrt::hstring CommitFull;
        winrt::hstring Branch;
        bool Dirty{ false };
        int64_t Timestamp{ 0 };
        winrt::hstring Payload;
        // Which producer staged this. Absent in a marker written before there
        // was more than one, which is exactly what a local deploy wrote, so
        // that is what an absent value means.
        winrt::hstring Source;
        // The marker this came from, so promotion can delete the one it
        // consumed rather than guessing at a name.
        std::filesystem::path MarkerPath;

        bool FromCI() const noexcept
        {
            return Source == L"ci";
        }

        std::wstring RelativeAge() const
        {
            return BuildInfo::RelativeAge(Timestamp);
        }

        // "a1b2c3d+dirty (main), 4 minutes ago" -- plus " from CI" when it did
        // not come off this machine. Worth the width: a local build can carry
        // uncommitted changes and a CI build never does, so when both are staged
        // this is the only thing that says which one the button will install.
        winrt::hstring Describe() const
        {
            return winrt::hstring{ fmt::format(FMT_COMPILE(L"{}{} ({}), {}{}"),
                                               std::wstring_view{ Commit },
                                               Dirty ? L"+dirty" : L"",
                                               std::wstring_view{ Branch },
                                               RelativeAge(),
                                               FromCI() ? L" from CI" : L"") };
        }
    };

    // nullopt when the marker is missing, unreadable or malformed. A missing
    // marker is the normal state on a machine that has never staged anything,
    // so it is not worth logging.
    inline std::optional<StagedBuild> ReadStagedFrom(const std::filesystem::path& markerPath)
    try
    {
        const auto contents{ til::io::read_file_as_utf8_string_if_exists(markerPath) };
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
        staged.Source = root.GetNamedString(L"source", L"local");
        staged.MarkerPath = markerPath;

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
        const auto sameCommit{ !staged.CommitFull.empty() &&
                               staged.CommitFull == winrt::hstring{ TERMINAL_BUILD_COMMIT_FULL } };

        // Same commit, and neither side carries uncommitted changes: this is a
        // second compile of source we are already running. Without this the
        // timestamp rule below would offer it, and keep offering it -- a CI
        // build of the commit you are on is always newer than your copy of it,
        // so the button would never go away no matter how often you pressed it.
        if (sameCommit && !staged.Dirty && TERMINAL_BUILD_DIRTY == 0)
        {
            return false;
        }

        if (!staged.CommitFull.empty() && !sameCommit)
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

    // Every marker in the slot root, newest first. A directory that does not
    // exist is the normal state on a machine that has never staged anything.
    inline std::vector<StagedBuild> ReadAllStaged()
    try
    {
        std::vector<StagedBuild> found;
        std::error_code error;
        std::filesystem::directory_iterator entries{ std::filesystem::path{ SlotRoot }, error };
        if (error)
        {
            return found;
        }

        for (const auto& entry : entries)
        {
            if (!entry.is_regular_file(error))
            {
                continue;
            }
            if (!IsPendingMarkerName(entry.path().filename()))
            {
                continue;
            }
            if (auto staged{ ReadStagedFrom(entry.path()) })
            {
                found.push_back(std::move(*staged));
            }
        }

        std::sort(found.begin(), found.end(), [](const auto& left, const auto& right) {
            return left.Timestamp > right.Timestamp;
        });
        return found;
    }
    catch (...)
    {
        LOG_CAUGHT_EXCEPTION();
        return {};
    }

    // The staged build worth offering, or nullopt if there is nothing to offer.
    //
    // Newest wins across every producer. A local deploy and a fetched CI build
    // can both be staged at once -- on a machine that uses both, or simply
    // because the loser's marker is still sitting there -- and the older one is
    // not a second offer, it is history.
    inline std::optional<StagedBuild> PendingPromotion()
    {
        if (!ThisSlotCanBePromoted())
        {
            return std::nullopt;
        }

        for (auto& staged : ReadAllStaged())
        {
            if (DiffersFromRunning(staged))
            {
                return staged;
            }
        }
        return std::nullopt;
    }
}
