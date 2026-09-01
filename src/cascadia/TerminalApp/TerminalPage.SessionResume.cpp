// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Detecting which panes are running a recognized session multiplexer/agent
// (shefrd, herdr, tmux, screen, zellij), for the opt-in resume-on-restart
// feature. See RecognizedSessions.h for the detection itself, and
// WindowEmperor.cpp for where this gets called and what happens with the
// result -- this file only answers "what's resumable right now", it never
// writes anything to disk itself.

#include "pch.h"
#include "TerminalPage.h"
#include "Tab.h"
#include "RecognizedSessions.h"

using namespace winrt::Microsoft::Terminal::TerminalConnection;
using namespace winrt::Windows::Data::Json;

namespace winrt::TerminalApp::implementation
{
    // Walks every pane in every tab and returns the ones running a
    // recognized session, as a JSON array: [{"processName":..., "resumeArgs":
    // [...], "cwd":...}, ...]. Empty string if the setting is off or nothing
    // was recognized -- never null, so a caller can always treat the result
    // as "parse this or there's nothing here" without a separate null check.
    //
    // Blocking: RecognizedSessions::Recognize can shell out to WSL and wait
    // up to a few seconds per call. Only ever called from
    // WindowEmperor::_finalizeSessionPersistence, on the way out -- never
    // from the periodic save timer, which would turn this into a repeating
    // stutter during normal use instead of a one-time pause on exit.
    //
    // Never throws: a bug in here must not be able to interfere with normal
    // shutdown or, on the next launch, with normal startup.
    hstring TerminalPage::GetResumableSessionsJson()
    try
    {
        if (!_settings.GlobalSettings().ResumeRecognizedSessions())
        {
            return {};
        }

        JsonArray sessions;

        for (const auto& tab : _tabs)
        {
            const auto tabImpl = _GetTabImpl(tab);
            if (!tabImpl)
            {
                continue;
            }
            const auto rootPane = tabImpl->GetRootPane();
            if (!rootPane)
            {
                continue;
            }

            // Same walk ControlPipe.cpp already uses to reach a pane's live
            // TermControl -- proven, not a new traversal to get wrong.
            rootPane->WalkTree([&](const auto& pane) {
                const auto control = pane->GetTerminalControl();
                if (!control)
                {
                    return;
                }
                const auto connection = control.Connection();
                if (!connection)
                {
                    return;
                }
                const auto conpty = connection.try_as<ConptyConnection>();
                if (!conpty)
                {
                    return;
                }
                const auto handle = conpty.RootProcessHandle();
                if (!handle)
                {
                    return;
                }
                const auto pid = ::GetProcessId(reinterpret_cast<HANDLE>(handle));
                if (!pid)
                {
                    return;
                }

                const auto recognized = ::TerminalApp::RecognizedSessions::Recognize(pid);
                if (!recognized)
                {
                    return;
                }

                JsonArray argsArray;
                for (const auto& arg : recognized->ResumeArgs)
                {
                    argsArray.Append(JsonValue::CreateStringValue(arg));
                }

                JsonObject obj;
                obj.SetNamedValue(L"processName", JsonValue::CreateStringValue(recognized->ProcessName));
                obj.SetNamedValue(L"resumeArgs", argsArray);
                obj.SetNamedValue(L"cwd", JsonValue::CreateStringValue(control.WorkingDirectory()));
                sessions.Append(obj);
            });
        }

        return sessions.Size() > 0 ? sessions.Stringify() : hstring{};
    }
    catch (...)
    {
        LOG_CAUGHT_EXCEPTION();
        return {};
    }
}
