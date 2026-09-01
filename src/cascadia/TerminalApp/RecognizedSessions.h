// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Recognizing whether a pane is currently running a session multiplexer or
// agent runtime -- shefrd, herdr, tmux, screen, zellij -- rather than a bare
// shell, so the resume-on-restart feature knows which panes are safe to
// bring back and what command actually reattaches them.
//
// Nothing in this codebase tracks a pane's LIVE process before this: what
// gets persisted today (TerminalPaneContent::GetNewTerminalArgs) is the
// commandline a pane was launched with, never what it's currently running.
// This walks the OS process tree directly, rooted at the pane's own client
// process (ConptyConnection::RootProcessHandle) -- the same technique
// tools\Test-DevSlotIdle.ps1 already uses externally via CIM to answer the
// same kind of question for the Dev slot's promote check. Keep the
// recognized-name list in sync with that script's $KnownMultiplexers.
//
// Every call here can block for the duration of a CreateToolhelp32Snapshot
// and, when a WSL leaf is found, a synchronous child-process launch to check
// inside it -- callers MUST NOT call this from the UI thread. Use
// winrt::resume_background() first.

#pragma once

#include <TlHelp32.h>
#include <algorithm>
#include <array>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <til/string.h>
#include <til/u8u16convert.h>

namespace TerminalApp::RecognizedSessions
{
    // Mirrors tools\Test-DevSlotIdle.ps1's $KnownMultiplexers -- keep both in sync.
    inline constexpr std::array<std::wstring_view, 5> KnownProcessNames{
        L"shefrd", L"herdr", L"tmux", L"screen", L"zellij"
    };

    // Which WSL distro to check for a WSL-hosted leaf process. Hardcoded for
    // the same reason Test-DevSlotIdle.ps1 defaults to Ubuntu rather than
    // reading it out of the leaf's own commandline: getting a live process's
    // full commandline back out of Windows needs NtQuerySystemInformation or
    // WMI, neither of which is worth adding just to discover a -d flag when
    // the common case is one default distro.
    inline constexpr std::wstring_view DefaultWslDistro{ L"Ubuntu" };

    struct Recognized
    {
        std::wstring ProcessName;
        // The command that reattaches: the bare process name for a native
        // leaf, or a `wsl.exe -d <distro> -- <name>` invocation for a WSL
        // one. Running it again is what "resuming" means here -- these are
        // daemon-backed multiplexers, so re-invoking the same command
        // reattaches to the already-running session rather than starting a
        // second one.
        std::wstring ResumeCommandline;
    };

    namespace details
    {
        struct ProcessInfo
        {
            DWORD Pid;
            DWORD ParentPid;
            std::wstring Name;
        };

        // One snapshot, every process on the system. Cheap enough to take
        // per pane at the cadence this is called (the periodic session-save
        // timer, a handful of times an hour at most) that there is no reason
        // to share one snapshot across panes and add lifetime concerns for
        // the saving.
        inline std::vector<ProcessInfo> SnapshotAllProcesses()
        {
            std::vector<ProcessInfo> processes;

            wil::unique_handle snapshot{ CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
            if (!snapshot)
            {
                return processes;
            }

            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snapshot.get(), &entry))
            {
                do
                {
                    processes.push_back(ProcessInfo{ entry.th32ProcessID, entry.th32ParentProcessID, entry.szExeFile });
                } while (Process32NextW(snapshot.get(), &entry));
            }

            return processes;
        }

        // Every direct and indirect child of rootPid, breadth-first.
        inline std::vector<ProcessInfo> DescendantsOf(DWORD rootPid, const std::vector<ProcessInfo>& all)
        {
            std::vector<ProcessInfo> result;
            std::vector<DWORD> frontier{ rootPid };

            while (!frontier.empty())
            {
                std::vector<DWORD> next;
                for (const auto& proc : all)
                {
                    if (std::find(frontier.begin(), frontier.end(), proc.ParentPid) != frontier.end())
                    {
                        result.push_back(proc);
                        next.push_back(proc.Pid);
                    }
                }
                frontier = std::move(next);
            }

            return result;
        }

        inline bool NameMatches(const std::wstring& exeFileName, std::wstring_view candidate)
        {
            // szExeFile carries the extension ("wsl.exe"); our own candidates never do.
            auto name = exeFileName;
            if (const auto dot = name.rfind(L'.'); dot != std::wstring::npos)
            {
                name.resize(dot);
            }
            return til::equals_insensitive_ascii(name, candidate);
        }

        // Runs a command with no window of any kind (CREATE_NO_WINDOW, not just
        // SW_HIDE -- there is no console for a delegation handoff to intercept
        // in the first place, which is the same class of bug fixed for the CI
        // poller's scheduled task; see CLAUDE.md's Invoke-Hidden.vbs section)
        // and returns its stdout as a string, or nullopt if it could not run.
        // Blocks the calling thread until the child exits or the timeout hits.
        inline std::optional<std::string> RunHiddenCaptureStdout(std::wstring commandLine, DWORD timeoutMs = 5000)
        {
            SECURITY_ATTRIBUTES saAttr{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };

            wil::unique_handle readPipe;
            wil::unique_handle writePipe;
            if (!CreatePipe(readPipe.addressof(), writePipe.addressof(), &saAttr, 0))
            {
                return std::nullopt;
            }
            // The write end must not be inherited by anything except the child
            // we are about to spawn's stdout slot, or the read below never sees EOF.
            SetHandleInformation(readPipe.get(), HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOW startupInfo{};
            startupInfo.cb = sizeof(startupInfo);
            startupInfo.dwFlags = STARTF_USESTDHANDLES;
            startupInfo.hStdOutput = writePipe.get();
            startupInfo.hStdError = writePipe.get();
            startupInfo.hStdInput = nullptr;

            wil::unique_process_information procInfo;
            const auto ok = CreateProcessW(nullptr,
                                            commandLine.data(),
                                            nullptr,
                                            nullptr,
                                            TRUE, // inherit handles, just the pipe
                                            CREATE_NO_WINDOW,
                                            nullptr,
                                            nullptr,
                                            &startupInfo,
                                            &procInfo);
            // Close our copy of the write end regardless of outcome: the child's
            // copy is what keeps it open until it exits, ours must not.
            writePipe.reset();

            if (!ok)
            {
                return std::nullopt;
            }

            std::string output;
            char buffer[4096];
            DWORD bytesRead{ 0 };
            while (ReadFile(readPipe.get(), buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
            {
                output.append(buffer, bytesRead);
            }

            WaitForSingleObject(procInfo.hProcess, timeoutMs);
            return output;
        }

        // A WSL leaf hides everything inside a separate Linux process
        // namespace invisible to CreateToolhelp32Snapshot, so the only way to
        // ask "is a known multiplexer running in there" is to ask WSL itself.
        // Same command shape as Test-DevSlotIdle.ps1's WSL-side check
        // (ps -eo comm | grep -E ... | sort | uniq -c), reused rather than a
        // fresh pgrep variant: one already-proven-working invocation instead
        // of a new untested one. Requires exactly one total match, same
        // reasoning as that script: more than one is ambiguous (which one
        // does this pane's wsl.exe actually own?) and is "not recognized"
        // rather than a guess.
        inline std::optional<Recognized> CheckWslLeaf()
        {
            std::wstring pattern;
            for (size_t i = 0; i < KnownProcessNames.size(); ++i)
            {
                if (i > 0)
                {
                    pattern += L'|';
                }
                pattern += KnownProcessNames[i];
            }

            auto commandLine = fmt::format(LR"(wsl.exe -d {} -- bash -c "ps -eo comm | grep -E '^({})$' | sort | uniq -c")", DefaultWslDistro, pattern);
            const auto output = RunHiddenCaptureStdout(std::move(commandLine));
            if (!output || output->empty())
            {
                return std::nullopt;
            }

            // Each line is "<count> <name>" (uniq -c's format, leading
            // whitespace included). Exactly one line, whose count is exactly
            // 1, is the only unambiguous case.
            std::istringstream lines{ *output };
            std::string line;
            std::string matchedName;
            size_t lineCount{ 0 };
            while (std::getline(lines, line))
            {
                if (line.find_first_not_of(" \t\r\n") == std::string::npos)
                {
                    continue;
                }
                ++lineCount;
                std::istringstream parts{ line };
                int count{ 0 };
                std::string name;
                parts >> count >> name;
                if (count == 1)
                {
                    matchedName = name;
                }
            }

            if (lineCount != 1 || matchedName.empty())
            {
                return std::nullopt;
            }

            const auto wideName = til::u8u16(matchedName);
            return Recognized{
                wideName,
                fmt::format(L"wsl.exe -d {} -- {}", DefaultWslDistro, wideName)
            };
        }
    }

    // Fails closed on anything ambiguous, same principle as
    // tools\Test-DevSlotIdle.ps1: more than one candidate leaf under the
    // pane's process, or more than one known process matching inside WSL,
    // is "not recognized" rather than a guess. MUST be called off the UI
    // thread -- see the file header.
    inline std::optional<Recognized> Recognize(DWORD rootProcessId)
    {
        const auto all = details::SnapshotAllProcesses();
        const auto descendants = details::DescendantsOf(rootProcessId, all);

        // The leaves under a pane's client process are its shell hosts, same
        // shape as the Windows-side half of Test-DevSlotIdle.ps1: one wsl.exe
        // (or one native shell) per pane is the expected case, and conpty
        // plumbing (OpenConsole.exe, conhost.exe) is not a session by itself.
        constexpr std::array<std::wstring_view, 4> nativeLeafNames{ L"wsl", L"cmd", L"powershell", L"pwsh" };
        std::vector<const details::ProcessInfo*> leaves;
        for (const auto& proc : descendants)
        {
            for (const auto& leafName : nativeLeafNames)
            {
                if (details::NameMatches(proc.Name, leafName))
                {
                    leaves.push_back(&proc);
                    break;
                }
            }
        }

        if (leaves.size() != 1)
        {
            return std::nullopt;
        }

        if (!details::NameMatches(leaves.front()->Name, L"wsl"))
        {
            // A native leaf: check ITS descendants for a known name directly.
            const auto nativeDescendants = details::DescendantsOf(leaves.front()->Pid, all);
            for (const auto& proc : nativeDescendants)
            {
                for (const auto& known : KnownProcessNames)
                {
                    if (details::NameMatches(proc.Name, known))
                    {
                        return Recognized{ std::wstring{ known }, std::wstring{ known } };
                    }
                }
            }
            return std::nullopt;
        }

        return details::CheckWslLeaf();
    }
}
