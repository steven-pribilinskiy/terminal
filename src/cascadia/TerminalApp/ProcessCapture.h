// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Running a child process to completion and reading everything it wrote,
// with a deadline.
//
// Factored out of PaneSessionCapture's WSL probe, which is still its largest
// caller. The integration fetch pipeline needs the same thing for its
// `command` steps, and the two must not drift: the CREATE_NO_WINDOW and the
// polled read below are both load-bearing, and neither is obvious.

#pragma once

#include <string>
#include <string_view>

namespace TerminalApp
{
    // Blocking. Launches `commandLine`, writes `stdinData` to the child's stdin
    // (nothing at all when it is empty), and reads stdout+stderr to EOF or to
    // the deadline, whichever comes first. A child still running at the
    // deadline is terminated.
    //
    // Returns what was read, which is empty when the process could not be
    // started. The bytes are whatever the child wrote -- no decoding happens
    // here, because the caller knows whether it expects UTF-8.
    //
    // The command line is taken by value because CreateProcessW insists on a
    // writable buffer and may modify it in place.
    std::string RunProcessCapture(std::wstring commandLine, std::string_view stdinData, unsigned long timeoutMs);
}
