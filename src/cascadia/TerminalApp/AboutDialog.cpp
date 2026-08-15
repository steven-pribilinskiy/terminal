
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "AboutDialog.h"
#include "AboutDialog.g.cpp"

#include <WtExeUtils.h>
#include <TerminalBuildInfo.h>

#include "../../types/inc/utils.hpp"
#include "Utils.h"

using namespace winrt;
using namespace winrt::Microsoft::Terminal::Settings::Model;
using namespace winrt::Microsoft::Terminal;
using namespace ::TerminalApp;
using namespace std::chrono_literals;

namespace winrt
{
    namespace WUX = Windows::UI::Xaml;
    using IInspectable = Windows::Foundation::IInspectable;
}

namespace winrt::TerminalApp::implementation
{
    AboutDialog::AboutDialog()
    {
        InitializeComponent();
        _queueUpdateCheck();
    }

    winrt::hstring AboutDialog::ApplicationDisplayName()
    {
        return CascadiaSettings::ApplicationDisplayName();
    }

    winrt::hstring AboutDialog::ApplicationVersion()
    {
        return CascadiaSettings::ApplicationVersion();
    }

    // The commit this binary was built from, with a marker when the working tree
    // had uncommitted changes -- in that case the hash alone doesn't identify it.
    winrt::hstring AboutDialog::BuildCommit()
    {
#if TERMINAL_BUILD_DIRTY
        return winrt::hstring{ TERMINAL_BUILD_COMMIT L"+dirty" };
#else
        return winrt::hstring{ TERMINAL_BUILD_COMMIT };
#endif
    }

    winrt::hstring AboutDialog::BuildBranch()
    {
        return winrt::hstring{ TERMINAL_BUILD_BRANCH };
    }

    // "5 days ago (2026-08-10 15:56 UTC)" -- the relative part is what tells you
    // at a glance whether the window predates the build you just made.
    winrt::hstring AboutDialog::BuildTime()
    {
        const auto built{ std::chrono::system_clock::from_time_t(static_cast<std::time_t>(TERMINAL_BUILD_TIMESTAMP)) };
        const auto elapsed{ std::chrono::system_clock::now() - built };
        const auto seconds{ std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() };

        std::wstring relative;
        if (seconds < 0)
        {
            // Clock skew, or a binary copied from a machine set ahead of this one.
            relative = L"just now";
        }
        else if (seconds < 60)
        {
            relative = L"just now";
        }
        else if (seconds < 3600)
        {
            const auto n{ seconds / 60 };
            relative = fmt::format(FMT_COMPILE(L"{} minute{} ago"), n, n == 1 ? L"" : L"s");
        }
        else if (seconds < 86400)
        {
            const auto n{ seconds / 3600 };
            relative = fmt::format(FMT_COMPILE(L"{} hour{} ago"), n, n == 1 ? L"" : L"s");
        }
        else
        {
            const auto n{ seconds / 86400 };
            relative = fmt::format(FMT_COMPILE(L"{} day{} ago"), n, n == 1 ? L"" : L"s");
        }

        return winrt::hstring{ fmt::format(FMT_COMPILE(L"{} ({})"), relative, TERMINAL_BUILD_TIMESTAMP_STRING) };
    }

    // One line with everything needed to identify this binary, for pasting into
    // a bug report.
    winrt::hstring AboutDialog::BuildInfoForClipboard()
    {
        return winrt::hstring{ fmt::format(FMT_COMPILE(L"{} {} ({}, {}) built {}"),
                                           ApplicationDisplayName(),
                                           ApplicationVersion(),
                                           BuildCommit(),
                                           BuildBranch(),
                                           TERMINAL_BUILD_TIMESTAMP_STRING) };
    }

    void AboutDialog::_CopyBuildInfoOnClick(const IInspectable& /*sender*/, const Windows::UI::Xaml::RoutedEventArgs& /*eventArgs*/)
    {
        Windows::ApplicationModel::DataTransfer::DataPackage package;
        package.SetText(BuildInfoForClipboard());
        Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
    }

    void AboutDialog::_SendFeedbackOnClick(const IInspectable& /*sender*/, const Windows::UI::Xaml::Controls::ContentDialogButtonClickEventArgs& /*eventArgs*/)
    {
#if defined(WT_BRANDING_RELEASE)
        ShellExecute(nullptr, nullptr, L"https://go.microsoft.com/fwlink/?linkid=2125419", nullptr, nullptr, SW_SHOW);
#else
        ShellExecute(nullptr, nullptr, L"https://go.microsoft.com/fwlink/?linkid=2204904", nullptr, nullptr, SW_SHOW);
#endif
    }

    void AboutDialog::_ThirdPartyNoticesOnClick(const IInspectable& /*sender*/, const Windows::UI::Xaml::RoutedEventArgs& /*eventArgs*/)
    {
        std::filesystem::path currentPath{ wil::GetModuleFileNameW<std::wstring>(nullptr) };
        currentPath.replace_filename(L"NOTICE.html");
        ShellExecute(nullptr, nullptr, currentPath.c_str(), nullptr, nullptr, SW_SHOW);
    }

    safe_void_coroutine AboutDialog::_queueUpdateCheck()
    {
        auto strongThis = get_strong();
        auto now{ std::chrono::system_clock::now() };
        if (now - _lastUpdateCheck < std::chrono::days{ 1 })
        {
            co_return;
        }
        _lastUpdateCheck = now;

        if (!IsPackaged())
        {
            co_return;
        }

        co_await wil::resume_foreground(strongThis->Dispatcher());
        UpdatesAvailable(false);
        CheckingForUpdates(true);

        try
        {
#ifdef WT_BRANDING_DEV
            // **DEV BRANDING**: Always sleep for three seconds and then report that
            // there is an update available. This lets us test the system.
            co_await winrt::resume_after(std::chrono::seconds{ 3 });
            co_await wil::resume_foreground(strongThis->Dispatcher());
            UpdateStatusText(RS_(L"AboutDialog_UpdateSimulated"));
            UpdatesAvailable(true);
#else // release build, likely has a store context
            bool packageManagerAnswered{ false };

            try
            {
                if (auto currentPackage{ winrt::Windows::ApplicationModel::Package::Current() })
                {
                    // We need to look up our package in the Package Manager; we cannot use Current
                    winrt::Windows::Management::Deployment::PackageManager pm;
                    if (auto lookedUpPackage{ pm.FindPackageForUser(winrt::hstring{}, currentPackage.Id().FullName()) })
                    {
                        using winrt::Windows::ApplicationModel::PackageUpdateAvailability;
                        auto availabilityResult = co_await lookedUpPackage.CheckUpdateAvailabilityAsync();
                        co_await wil::resume_foreground(strongThis->Dispatcher());
                        auto availability = availabilityResult.Availability();
                        switch (availability)
                        {
                        case PackageUpdateAvailability::Available:
                        case PackageUpdateAvailability::Required:
                        case PackageUpdateAvailability::NoUpdates:
                            UpdateStatusText(availability == PackageUpdateAvailability::Required ?
                                                 RS_(L"AboutDialog_UpdateFromPackageRequired") :
                                                 RS_(L"AboutDialog_UpdateFromPackage"));
                            UpdatesAvailable(availability != PackageUpdateAvailability::NoUpdates);
                            packageManagerAnswered = true;
                            break;
                        case PackageUpdateAvailability::Error:
                        case PackageUpdateAvailability::Unknown:
                        default:
                            // Do not set packageManagerAnswered, which will trigger the store check.
                            break;
                        }
                    }
                }
            }
            catch (...)
            {
            } // Do nothing on failure

            if (!packageManagerAnswered)
            {
                if (auto storeContext{ winrt::Windows::Services::Store::StoreContext::GetDefault() })
                {
                    const auto updates = co_await storeContext.GetAppAndOptionalStorePackageUpdatesAsync();
                    co_await wil::resume_foreground(strongThis->Dispatcher());
                    if (updates)
                    {
                        const auto numUpdates = updates.Size();
                        if (numUpdates > 0)
                        {
                            UpdateStatusText(RS_(L"AboutDialog_UpdateFromStore"));
                            UpdatesAvailable(true);
                        }
                    }
                }
            }
#endif
        }
        catch (...)
        {
            // do nothing on failure
        }

        co_await wil::resume_foreground(strongThis->Dispatcher());
        CheckingForUpdates(false);
    }
}
