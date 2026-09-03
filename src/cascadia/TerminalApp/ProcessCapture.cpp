// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "ProcessCapture.h"

#include <algorithm>

namespace TerminalApp
{
    std::string RunProcessCapture(std::wstring commandLine, std::string_view stdinData, unsigned long timeoutMs)
    {
        if (commandLine.empty())
        {
            return {};
        }

        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };

        wil::unique_handle inRead, inWrite, outRead, outWrite;
        if (!CreatePipe(inRead.addressof(), inWrite.addressof(), &sa, 0) ||
            !CreatePipe(outRead.addressof(), outWrite.addressof(), &sa, 0))
        {
            return {};
        }
        SetHandleInformation(inWrite.get(), HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(outRead.get(), HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = inRead.get();
        si.hStdOutput = outWrite.get();
        // Merged into stdout rather than left null: STARTF_USESTDHANDLES hands
        // the child exactly these three, and a null stderr is an invalid handle
        // it may refuse to start with. Anything the child complains about lands
        // in the output, where the caller's parser drops it.
        si.hStdError = outWrite.get();

        // CREATE_NO_WINDOW matters on a machine where Windows Terminal is the
        // registered default terminal host: a console child launched without it
        // gets a Terminal window created for it before any hide request could
        // apply. See "Every hidden background launch" in the repo's CLAUDE.md.
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            return {};
        }
        wil::unique_handle process{ pi.hProcess };
        wil::unique_handle thread{ pi.hThread };

        // Close our copies of the child's ends first, or the reads below never
        // see EOF because this process still holds the pipe open.
        inRead.reset();
        outWrite.reset();

        if (!stdinData.empty())
        {
            DWORD written = 0;
            WriteFile(inWrite.get(), stdinData.data(), gsl::narrow_cast<DWORD>(stdinData.size()), &written, nullptr);
        }
        inWrite.reset();

        // Polled rather than a plain blocking read loop, because a blocking
        // ReadFile cannot be given a deadline: if the child is cold, wedged, or
        // mid-shutdown, the read never returns and the timeout below never gets
        // to run. Some callers run while the app is closing, so "wait forever"
        // is not an option any of them may take.
        std::string output;
        char buffer[4096];
        const auto deadline = GetTickCount64() + timeoutMs;

        for (;;)
        {
            DWORD available = 0;
            if (!PeekNamedPipe(outRead.get(), nullptr, 0, nullptr, &available, nullptr))
            {
                // The child closed its end: everything it wrote is read.
                break;
            }

            if (available == 0)
            {
                if (GetTickCount64() > deadline)
                {
                    TerminateProcess(process.get(), 1);
                    break;
                }
                Sleep(20);
                continue;
            }

            DWORD read = 0;
            const auto want = std::min(static_cast<DWORD>(sizeof(buffer)), available);
            if (!ReadFile(outRead.get(), buffer, want, &read, nullptr) || read == 0)
            {
                break;
            }
            output.append(buffer, read);
        }

        WaitForSingleObject(process.get(), 1000);
        return output;
    }
}
