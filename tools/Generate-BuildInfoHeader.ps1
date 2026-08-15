# Emits a C++ header describing which commit this build came from and when it
# was built, on stdout. The caller (build/rules/GenerateBuildInfo.proj) captures
# the output and writes the file, mirroring Generate-FeatureStagingHeader.ps1.
#
# Everything degrades to a placeholder rather than failing the build: a source
# archive with no .git, or a machine with no git on PATH, still compiles.
[CmdletBinding()]
Param(
    [string]$Branding = "Dev"
)

$ErrorActionPreference = 'Continue'

function Invoke-Git {
    Param([string[]]$Arguments, [string]$Fallback = 'unknown')
    try {
        $out = & git.exe @Arguments 2>$null
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($out)) { return $Fallback }
        return ($out | Select-Object -First 1).Trim()
    } catch {
        return $Fallback
    }
}

$commitShort = Invoke-Git @('rev-parse', '--short=9', 'HEAD')
$commitFull  = Invoke-Git @('rev-parse', 'HEAD')
$branch      = Invoke-Git @('rev-parse', '--abbrev-ref', 'HEAD')

# A non-empty porcelain listing means the tree had uncommitted changes when this
# was built, so the commit hash alone does not identify the binary.
$status = ''
try {
    $status = (& git.exe status --porcelain 2>$null) -join ''
} catch {
    $status = ''
}
$dirty = if ([string]::IsNullOrWhiteSpace($status)) { 0 } else { 1 }

$now      = [DateTimeOffset]::UtcNow
$unix     = $now.ToUnixTimeSeconds()
$readable = $now.UtcDateTime.ToString('yyyy-MM-dd HH:mm:ss') + ' UTC'

# Escaping: hashes and branch names can contain characters that are awkward in a
# C++ string literal (a branch is only barred from a small set). Backslash and
# double quote are the two that would break the literal.
function ConvertTo-CppLiteral {
    Param([string]$Value)
    return $Value.Replace('\', '\\').Replace('"', '\"')
}

@"
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// GENERATED FILE -- DO NOT EDIT.
// Produced by tools/Generate-BuildInfoHeader.ps1 during the build.

#pragma once

#define TERMINAL_BUILD_COMMIT L"$(ConvertTo-CppLiteral $commitShort)"
#define TERMINAL_BUILD_COMMIT_FULL L"$(ConvertTo-CppLiteral $commitFull)"
#define TERMINAL_BUILD_BRANCH L"$(ConvertTo-CppLiteral $branch)"
#define TERMINAL_BUILD_BRANDING L"$(ConvertTo-CppLiteral $Branding)"

// 1 when the working tree had uncommitted changes at build time.
#define TERMINAL_BUILD_DIRTY $dirty

// Seconds since the Unix epoch, UTC, at the moment this header was generated.
#define TERMINAL_BUILD_TIMESTAMP ${unix}LL
#define TERMINAL_BUILD_TIMESTAMP_STRING L"$readable"
"@
