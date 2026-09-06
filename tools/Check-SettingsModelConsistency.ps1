<#
Copyright (c) Microsoft Corporation.
Licensed under the MIT license.
.SYNOPSIS
Checks the Settings Model's parallel lists agree with each other.

.DESCRIPTION
A setting in this codebase is spelled out in several files that nothing forces
to agree: the X-macro lists in MTSMSettings.h, the WinRT projection in
GlobalAppSettings.idl, a JSON mapper for each enum type, and an EnumMappings
entry for each enum a settings page shows in a dropdown.

Most disagreements are caught by the compiler. The ones that are not fail at
runtime, in the Settings Model, when the settings page that needs them is built
-- which is to say after a 40-minute CI build, on a machine that cannot build
locally. This catches those in about a second.

It is deliberately textual rather than a real parse: it has to run without a
build, and the files are regular enough that regexes are honest here.

.EXAMPLE
pwsh -File tools\Check-SettingsModelConsistency.ps1
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)][string]$SourceDir = "$PSScriptRoot\..\src\cascadia\TerminalSettingsModel"
)

$ErrorActionPreference = 'Stop'
$problems = [System.Collections.Generic.List[string]]::new()

function Read-SourceFile([string]$name) {
    $path = Join-Path $SourceDir $name
    if (-not (Test-Path $path)) { throw "missing $path" }
    Get-Content -Raw -LiteralPath $path
}

$mtsm = Read-SourceFile 'MTSMSettings.h'
$idl = Read-SourceFile 'GlobalAppSettings.idl'
$helpers = Read-SourceFile 'TerminalSettingsSerializationHelpers.h'
$enumCpp = Read-SourceFile 'EnumMappings.cpp'
$enumIdl = Read-SourceFile 'EnumMappings.idl'

# ---- 1. Every X(type, Name, "key", ...) has an INHERITABLE_SETTING(_, Name) ----
#
# A setting present in the macro but absent from the projection compiles: the
# implementation gets the property, and the WinRT interface does not. Anything
# reaching it through the projection -- which is every settings page -- does not
# see it.
# Only the two lists GlobalAppSettings actually projects. MTSMSettings.h also
# holds the profile, appearance and theme lists, which are projected by their own
# runtimeclasses and would otherwise all read as missing.
$globalLists = ''
foreach ($listName in 'MTSM_GLOBAL_ONLY_SETTINGS', 'MTSM_WINDOW_SETTINGS') {
    $m = [regex]::Match($mtsm, "(?s)#define\s+$listName\(X\)(.*?)\r?\n\r?\n")
    if (-not $m.Success) {
        $problems.Add("could not find the $listName block in MTSMSettings.h -- this checker needs updating")
        continue
    }
    $globalLists += $m.Groups[1].Value
}

$settings = [regex]::Matches($globalLists, '(?m)^\s*X\(\s*([^,]+?)\s*,\s*(\w+)\s*,\s*"([^"]+)"')
$projected = [regex]::Matches($idl, 'INHERITABLE_SETTING\(\s*[^,]+,\s*(\w+)\s*\)') |
    ForEach-Object { $_.Groups[1].Value }

# Known and correct exceptions. Both are projected or converted by hand rather
# than through the usual macro, so the textual check cannot see them.
$notProjectedByMacro = @('Integrations')
$noEnumMapper = @('ThemePair')

foreach ($s in $settings) {
    $name = $s.Groups[2].Value
    if ($notProjectedByMacro -contains $name) { continue }
    if ($projected -notcontains $name) {
        $problems.Add("MTSMSettings.h declares '$name' (json key '$($s.Groups[3].Value)') but GlobalAppSettings.idl has no INHERITABLE_SETTING for it")
    }
}

# ---- 2. Every JSON_MAPPINGS(n) declares as many entries as it lists ----
#
# The count is the array's size. Declare more than you list and the tail is
# value-initialised: pair_type{ nullptr, ... }. Iterating those and calling
# to_hstring on a null string_view is an access violation inside the Settings
# Model, raised the moment a page asks EnumMappings for that type.
foreach ($m in [regex]::Matches($helpers, '(?s)JSON_(?:ENUM|FLAG)_MAPPER\(([^)]*)\).*?JSON_MAPPINGS\((\d+)\)\s*=\s*\{(.*?)\n\s*\};')) {
    $type = ($m.Groups[1].Value -split '::')[-1]
    $declared = [int]$m.Groups[2].Value
    $actual = ([regex]::Matches($m.Groups[3].Value, 'pair_type\{')).Count
    if ($declared -ne $actual) {
        $problems.Add("JSON mapper for '$type' declares JSON_MAPPINGS($declared) but lists $actual entries")
    }
}

# Same shape, spelled the long way rather than through the macro.
foreach ($m in [regex]::Matches($helpers, '(?s)JSON_(?:ENUM|FLAG)_MAPPER\(([^)]*)\).*?std::array<pair_type,\s*(\d+)>\s*mappings\s*=\s*\{(.*?)\n\s*\};')) {
    $type = ($m.Groups[1].Value -split '::')[-1]
    $declared = [int]$m.Groups[2].Value
    $actual = ([regex]::Matches($m.Groups[3].Value, 'pair_type\{')).Count
    if ($declared -ne $actual) {
        $problems.Add("JSON mapper for '$type' declares std::array<pair_type, $declared> but lists $actual entries")
    }
}

# ---- 3. Every EnumMappings entry in the IDL has a definition ----
#
# The IDL is what the projection exposes; the definition comes either from
# DEFINE_ENUM_MAP or from a hand-written function. Declared without either, the
# call fails at runtime rather than at build.
$declaredMaps = [regex]::Matches($enumIdl, 'static\s+Windows\.Foundation\.Collections\.IMap<[^>]+>\s+(\w+)\s*\{\s*get;\s*\}') |
    ForEach-Object { $_.Groups[1].Value }

foreach ($name in $declaredMaps) {
    $viaMacro = $enumCpp -match "DEFINE_ENUM_MAP\([^)]*,\s*$name\s*\)"
    $viaHand = $enumCpp -match "EnumMappings::$name\s*\("
    if (-not ($viaMacro -or $viaHand)) {
        $problems.Add("EnumMappings.idl exposes '$name' but EnumMappings.cpp neither DEFINE_ENUM_MAPs nor defines it")
    }
}

# ---- 4. Every enum a setting uses has a JSON mapper ----
foreach ($s in $settings) {
    $type = $s.Groups[1].Value
    if ($type -notmatch '^(bool|int32_t|uint32_t|int64_t|float|double|hstring|winrt::hstring)$' -and
        $type -notmatch 'IVector|IMap|Map$') {
        $leaf = ($type -split '::')[-1]
        if ($noEnumMapper -contains $leaf) { continue }
        if ($helpers -notmatch [regex]::Escape($leaf) ) {
            $problems.Add("setting '$($s.Groups[2].Value)' has type '$leaf' with no JSON mapper in TerminalSettingsSerializationHelpers.h")
        }
    }
}

Write-Host ("Checked {0} settings, {1} EnumMappings entries." -f $settings.Count, $declaredMaps.Count)

if ($problems.Count -gt 0) {
    Write-Host ''
    foreach ($p in $problems) { Write-Host "  FAIL: $p" -ForegroundColor Red }
    Write-Host ''
    exit 1
}

Write-Host 'No inconsistencies found.' -ForegroundColor Green
exit 0
