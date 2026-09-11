// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
#include "pch.h"
#include "ProcessCapture.h"
#include "../../inc/ProcessCaptureImpl.h"
namespace TerminalApp
{
    std::string RunProcessCapture(std::wstring commandLine, std::string_view stdinData, unsigned long timeoutMs)
    {
        return TerminalUtils::CaptureProcess(std::move(commandLine), stdinData, timeoutMs);
    }
}
