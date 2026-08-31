// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// The promote affordance for the local Dev slot: notice that a deploy has
// staged a newer build, offer it, and - only if asked - hand the swap to a
// detached helper.
//
// Why any of this exists: the Dev slot is where live sessions run, so a deploy
// deliberately does not touch it. It registers the Test slot, stages the Dev
// payload, and leaves the decision here, because only the person using the
// windows knows whether losing them is acceptable right now.

#include "pch.h"
#include "TerminalPage.h"
#include "BuildInfo.h"
#include "SlotPromotion.h"
#include "WindowListRequest.g.h"

#include <atomic>

#include <LibraryResources.h>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Xaml::Controls;

namespace winrt::TerminalApp::implementation
{
    // Ask whether anything is staged, and put the answer in the tab row. Cheap
    // enough to call whenever we might have missed a change; it reads one small
    // file and returns nothing at all outside the Dev slot.
    void TerminalPage::RefreshPendingPromotion()
    {
        if (!_tabRow)
        {
            return;
        }

        const auto pending{ ::TerminalApp::SlotPromotion::PendingPromotion() };
        _tabRow.PromotionAvailable(pending.has_value());
        _tabRow.PromotionDescription(pending ? pending->Describe() : hstring{});
    }

    // How much is about to be thrown away. The window count comes from the
    // emperor, which is the only thing that can see all of them; the pane count
    // is this window's, which is the one the user is looking at.
    hstring TerminalPage::_describePromotionCost()
    {
        uint32_t windowCount{ 1 };
        if (const auto request{ winrt::make<WindowListRequest>() })
        {
            RequestWindowList.raise(*this, request);
            if (const auto entries{ request.Entries() }; entries && entries.Size() > 0)
            {
                windowCount = entries.Size();
            }
        }

        uint32_t paneCount{ 0 };
        for (const auto& tab : _tabs)
        {
            if (const auto tabImpl{ _GetTabImpl(tab) })
            {
                paneCount += gsl::narrow_cast<uint32_t>(std::max(0, tabImpl->GetLeafPaneCount()));
            }
        }

        return hstring{ RS_fmt(L"PromoteSlotDialog_WindowsAndPanes", windowCount, paneCount) };
    }

    safe_void_coroutine TerminalPage::_PromoteSlotRequested(IInspectable /*sender*/, IInspectable /*args*/)
    {
        const auto weak{ get_weak() };

        // Re-read rather than trusting what lit the button: the deploy may have
        // run again, or been undone, since it appeared.
        const auto pending{ ::TerminalApp::SlotPromotion::PendingPromotion() };
        if (!pending)
        {
            RefreshPendingPromotion();
            co_return;
        }

        ContentDialog dialog{};
        dialog.Title(box_value(hstring{ RS_(L"PromoteSlotDialog_Title") }));
        dialog.Content(box_value(hstring{ RS_fmt(L"PromoteSlotDialog_Body",
                                                 std::wstring_view{ pending->Describe() },
                                                 std::wstring_view{ _describePromotionCost() }) }));
        dialog.PrimaryButtonText(RS_(L"PromoteSlotDialog_ApplyOnNextLaunch"));
        dialog.SecondaryButtonText(RS_(L"PromoteSlotDialog_ApplyNow"));
        dialog.CloseButtonText(RS_(L"PromoteSlotDialog_Cancel"));
        // Default to the button that destroys nothing. The primary action here
        // is disruptive enough that Enter must not be able to trigger the worst
        // of it by accident.
        dialog.DefaultButton(ContentDialogButton::Close);

        auto presenter{ _dialogPresenter.get() };
        if (!presenter)
        {
            co_return;
        }

        const auto result{ co_await presenter.ShowDialog(dialog) };

        // ShowDialog blocks until dismissed, so `this` may be gone by now.
        const auto strong{ weak.get() };
        if (!strong)
        {
            co_return;
        }

        if (result == ContentDialogResult::None)
        {
            co_return;
        }

        const auto applyNow{ result == ContentDialogResult::Secondary };
        // Hand the helper the payload we actually offered. It has its own
        // defaults for being run by hand, and those name the local deploy's
        // directory -- so leaving it to guess would quietly promote a local
        // build when the dialog described a fetched one.
        if (!strong->_armPromotionHelper(applyNow, *pending))
        {
            co_return;
        }

        if (applyNow)
        {
            // The helper is now waiting on our process id. Quitting takes the
            // normal path, so layouts persist exactly as they would on any
            // other quit and come back when the new build starts.
            strong->RequestQuit();
        }
    }

    // Spawn the detached helper that performs the swap once we are gone.
    //
    // It has to be a separate process: re-registering a package identity from a
    // different folder means removing the old registration first, and Windows
    // will not remove a package that is still running. Nothing here can do that
    // to itself.
    //
    // Returns false if the helper could not be started, in which case we must
    // not quit - closing every window to accomplish nothing is the one outcome
    // worse than not promoting.
    bool TerminalPage::_armPromotionHelper(bool relaunch, const ::TerminalApp::SlotPromotion::StagedBuild& staged)
    try
    {
        const auto helper{ std::filesystem::path{ ::TerminalApp::SlotPromotion::SlotRoot } / L"Promote-DevSlot.ps1" };
        if (!std::filesystem::exists(helper))
        {
            LOG_HR_MSG(E_INVALIDARG, "control slot promotion: helper missing");
            return false;
        }

        const auto args{ fmt::format(LR"(-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "{}" -WaitForPid {} -Staged "{}" -Marker "{}"{})",
                                     helper.wstring(),
                                     GetCurrentProcessId(),
                                     std::wstring_view{ staged.Payload },
                                     staged.MarkerPath.wstring(),
                                     relaunch ? L" -Relaunch" : L"") };

        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = L"open";
        info.lpFile = L"powershell.exe";
        info.lpParameters = args.c_str();
        // The helper outlives us on purpose - it cannot start work until we are
        // gone - so SEE_MASK_NOASYNC matters: it keeps the launch from being
        // torn down when this process begins exiting.
        info.nShow = SW_HIDE;

        if (!ShellExecuteExW(&info))
        {
            LOG_LAST_ERROR();
            return false;
        }
        if (info.hProcess)
        {
            CloseHandle(info.hProcess);
        }
        return true;
    }
    catch (...)
    {
        LOG_CAUGHT_EXCEPTION();
        return false;
    }

    // Keep the CI-build-poller scheduled task in step with the user's configured
    // interval, so the setting in Compatibility.xaml is more than decoration.
    //
    // Only the Dev slot touches this: the task is one machine-wide Scheduled
    // Task, not one per slot, and it exists to keep the Dev slot's promote
    // button fresh. Letting the Test slot's own copy of the setting fight over
    // the same task would just mean whichever window started last wins, for no
    // benefit -- the Test slot has nothing that reads dev-pending*.json.
    //
    // Fire-and-forget like _armPromotionHelper, and for the same reason:
    // Register-ScheduledTask is a sub-second call, but nothing on this path
    // should ever block a window from opening. Install-CIBuildPoller.ps1's own
    // -Force registration is idempotent, so calling this once per window
    // startup -- guarded here to once per process -- costs nothing when the
    // interval hasn't changed.
    void TerminalPage::_ReconcileCiPollInterval()
    try
    {
        static std::atomic_bool reconciled{ false };
        if (reconciled.exchange(true))
        {
            return;
        }

        if (!::TerminalApp::SlotPromotion::ThisSlotCanBePromoted())
        {
            return;
        }

        const auto helper{ std::filesystem::path{ ::TerminalApp::SlotPromotion::SlotRoot } / L"Install-CIBuildPoller.ps1" };
        if (!std::filesystem::exists(helper))
        {
            return;
        }

        const auto intervalMinutes{ _settings.GlobalSettings().CiPollIntervalMinutes() };
        const auto args{ intervalMinutes > 0 ?
                             fmt::format(LR"(-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "{}" -IntervalMinutes {})", helper.wstring(), intervalMinutes) :
                             fmt::format(LR"(-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "{}" -Uninstall)", helper.wstring()) };

        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = L"open";
        info.lpFile = L"pwsh.exe";
        info.lpParameters = args.c_str();
        info.nShow = SW_HIDE;

        if (ShellExecuteExW(&info) && info.hProcess)
        {
            CloseHandle(info.hProcess);
        }
    }
    catch (...)
    {
        LOG_CAUGHT_EXCEPTION();
    }

    // The badge says which build this window is; this puts that answer on the
    // clipboard, because the place you need it is a bug report or a message to
    // someone else, and reading it off a tooltip to retype it is the whole
    // friction. Same text the About dialog copies -- see BuildInfo::ClipboardText.
    void TerminalPage::_CopyBuildInfoRequested(const IInspectable& /*sender*/, const IInspectable& /*args*/)
    {
        using namespace winrt::Windows::ApplicationModel::DataTransfer;
        using namespace winrt::Microsoft::Terminal::Settings::Model;

        DataPackage package;
        package.SetText(::TerminalApp::BuildInfo::ClipboardText(CascadiaSettings::ApplicationDisplayName(),
                                                                CascadiaSettings::ApplicationVersion()));
        Clipboard::SetContent(package);

        // The badge does not change when clicked, so without a confirmation a
        // successful copy and a dead button look identical.
        if (_buildInfoCopiedToast == nullptr)
        {
            if (auto tip{ FindName(L"BuildInfoCopiedToast").try_as<winrt::Microsoft::UI::Xaml::Controls::TeachingTip>() })
            {
                _buildInfoCopiedToast = std::make_shared<Toast>(tip);
                // IsLightDismissEnabled == true is bugged in Xaml Islands: another
                // window opening a tip dismisses this one immediately (MUX#4382).
                tip.IsLightDismissEnabled(false);
                tip.Closed({ get_weak(), &TerminalPage::_FocusActiveControl });
            }
        }
        _UpdateTeachingTipTheme(BuildInfoCopiedToast().try_as<winrt::Windows::UI::Xaml::FrameworkElement>());

        if (_buildInfoCopiedToast != nullptr)
        {
            _buildInfoCopiedToast->Open();
        }
    }
}
