// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Which build this is, and which local slot it is installed as. Shared by the
// About dialog and the tab row badge so the two cannot drift apart.
//
// The slot has to be resolved at RUNTIME, not from a branding #define: the Dev
// and Test slots are built from byte-identical binaries on purpose, so that a
// build verified in Test can be promoted into Dev unchanged. Package identity
// is the only thing that differs.

#pragma once

#include <TerminalBuildInfo.h>

namespace TerminalApp::BuildInfo
{
    enum class Slot
    {
        Dev,
        Test,
        Other, // Release/Preview/Canary, or unpackaged
    };

    inline Slot CurrentSlot() noexcept
    {
        try
        {
            const auto package{ winrt::Windows::ApplicationModel::Package::Current() };
            const std::wstring_view name{ package.Id().Name() };
            if (name == L"WindowsTerminalDev")
            {
                return Slot::Dev;
            }
            if (name == L"WindowsTerminalTest")
            {
                return Slot::Test;
            }
        }
        CATCH_LOG();
        return Slot::Other;
    }

    // Short, uppercase, meant to be readable at a glance in the tab row.
    inline winrt::hstring SlotBadge() noexcept
    {
        switch (CurrentSlot())
        {
        case Slot::Dev:
            return winrt::hstring{ L"DEV" };
        case Slot::Test:
            return winrt::hstring{ L"TEST" };
        default:
            return winrt::hstring{};
        }
    }

    inline winrt::hstring Commit()
    {
#if TERMINAL_BUILD_DIRTY
        return winrt::hstring{ TERMINAL_BUILD_COMMIT L"+dirty" };
#else
        return winrt::hstring{ TERMINAL_BUILD_COMMIT };
#endif
    }

    inline winrt::hstring Branch()
    {
        return winrt::hstring{ TERMINAL_BUILD_BRANCH };
    }

    // "5 days ago" / "20 minutes ago" for any unix timestamp. Shared so that a
    // build staged on disk and the build we are running are described in the
    // same words -- they get compared to each other by eye.
    inline std::wstring RelativeAge(int64_t unixSeconds)
    {
        const auto built{ std::chrono::system_clock::from_time_t(static_cast<std::time_t>(unixSeconds)) };
        const auto seconds{ std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - built).count() };

        if (seconds < 60)
        {
            // Also covers a negative delta from clock skew.
            return L"just now";
        }
        if (seconds < 3600)
        {
            const auto n{ seconds / 60 };
            return fmt::format(FMT_COMPILE(L"{} minute{} ago"), n, n == 1 ? L"" : L"s");
        }
        if (seconds < 86400)
        {
            const auto n{ seconds / 3600 };
            return fmt::format(FMT_COMPILE(L"{} hour{} ago"), n, n == 1 ? L"" : L"s");
        }
        const auto n{ seconds / 86400 };
        return fmt::format(FMT_COMPILE(L"{} day{} ago"), n, n == 1 ? L"" : L"s");
    }

    // How old the build we are running is -- the part that tells you at a
    // glance whether this window predates the build you just made.
    inline std::wstring RelativeBuildAge()
    {
        return RelativeAge(TERMINAL_BUILD_TIMESTAMP);
    }

    inline winrt::hstring BuildTime()
    {
        return winrt::hstring{ fmt::format(FMT_COMPILE(L"{} ({})"), RelativeBuildAge(), TERMINAL_BUILD_TIMESTAMP_STRING) };
    }

    // One line, everything needed to identify this binary in a bug report.
    inline winrt::hstring OneLine()
    {
        return winrt::hstring{ fmt::format(FMT_COMPILE(L"{} ({}, {}) built {}"),
                                           SlotBadge(),
                                           Commit(),
                                           Branch(),
                                           TERMINAL_BUILD_TIMESTAMP_STRING) };
    }
}
