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
    [Parameter(Mandatory = $false)][string]$SourceDir = "$PSScriptRoot\..\src\cascadia\TerminalSettingsModel",
    [Parameter(Mandatory = $false)][string]$EditorDir = "$PSScriptRoot\..\src\cascadia\TerminalSettingsEditor"
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

# ---- 5. Every action argument has a localized name ----
#
# ActionArgsMagic.h builds this key by pasting the argument's own name:
# LOCALIZED_NAME(Amount) becomes L"AmountActionArgumentLocalized". Nothing forces
# the string to exist, and a missing one does not degrade to a blank label --
# ResourceMap::GetValue hands back a null ResourceCandidate and RS_ calls
# ValueAsString() on it unguarded, so the process takes an access violation
# inside the Settings Model the moment anything asks an action for its name. The
# Settings UI asks for every action's name as it opens, which is why one missing
# string here reads as "opening Settings crashes the Terminal".
$actionArgs = Read-SourceFile 'ActionArgs.h'
$resw = Read-SourceFile 'Resources\en-US\Resources.resw'

$localizedArgNames = [regex]::Matches($resw, '<data name="(\w+)ActionArgumentLocalized"') |
    ForEach-Object { $_.Groups[1].Value }

$argNames = [regex]::Matches($actionArgs, '(?m)^\s*X\(\s*[^,]+?\s*,\s*(\w+)\s*,') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique

foreach ($arg in $argNames) {
    if ($localizedArgNames -notcontains $arg) {
        $problems.Add("action argument '$arg' has no '${arg}ActionArgumentLocalized' string in Resources.resw -- naming any action that carries it crashes the Settings Model")
    }
}

# ---- 6. Every resource key named in the source exists in the resw ----
#
# The same crash reached the ordinary way. RS_, RS_fmt, RS_switchable_ and
# USES_RESOURCE all bottom out in that one unguarded ValueAsString().
$definedStrings = [regex]::Matches($resw, '<data name="([^"]+)"') |
    ForEach-Object { $_.Groups[1].Value }

$keyPattern = 'USES_RESOURCE\(L"([^"]+)"|RS_(?:fmt)?\(L"([^"]+)"|RS_switchable_(?:fmt)?\(L"([^"]+)"'
$referencedKeys = Get-ChildItem $SourceDir -Recurse -Include *.cpp, *.h -File |
    Where-Object { $_.FullName -notmatch '\\Generated Files\\' } |
    ForEach-Object {
        [regex]::Matches((Get-Content -Raw -LiteralPath $_.FullName), $keyPattern) | ForEach-Object {
            @($_.Groups[1].Value, $_.Groups[2].Value, $_.Groups[3].Value) | Where-Object { $_ }
        }
    } | Sort-Object -Unique

foreach ($key in $referencedKeys) {
    if ($definedStrings -notcontains $key) {
        $problems.Add("resource key '$key' is used in the Settings Model but has no entry in Resources.resw -- looking it up crashes the Settings Model")
    }
}

# ---- 7. Every enum dropdown value has a label in the editor's resw ----
#
# The same crash one page deeper, and quieter. Utils.cpp's LocalizedNameForEnumName
# concatenates the key rather than spelling it -- prefix, then the EnumMappings key
# with its first letter uppercased, then the property -- and hands it to the same
# unguarded GetLibraryResourceString. INITIALIZE_BINDABLE_ENUM_SETTING runs it once
# per enum value while the view model is constructed, so a fumbled letter is not a
# blank dropdown entry, it is a dead process. Unlike the action-argument case this
# waits until you navigate to the page that owns the view model, which makes it
# likelier to ship, not less.
$editorResw = Get-Content -Raw -LiteralPath (Join-Path $EditorDir 'Resources\en-US\Resources.resw')
$editorStrings = [regex]::Matches($editorResw, '<data name="([^"]+)"') | ForEach-Object { $_.Groups[1].Value }

# The json spellings behind each enum, which is what EnumMappings hands out.
$keysForType = @{}
foreach ($m in [regex]::Matches($helpers, '(?s)JSON_(?:ENUM|FLAG)_MAPPER\(([^)]*)\)(.*?)\n\s*\};')) {
    $leaf = ($m.Groups[1].Value -split '::')[-1].Trim()
    $keysForType[$leaf] = [regex]::Matches($m.Groups[2].Value, 'pair_type\{\s*"([^"]+)"') |
        ForEach-Object { $_.Groups[1].Value }
}

# EnumMappings name -> the type it iterates, plus any legacy spellings it drops.
# DEFINE_ENUM_MAP takes them all; the hand-written ones filter with literal
# comparisons (enumStr != "footer"), which is exactly as parseable.
$mapSource = @{}
foreach ($m in [regex]::Matches($enumCpp, 'DEFINE_ENUM_MAP\(\s*([^,]+?)\s*,\s*(\w+)\s*\)')) {
    $mapSource[$m.Groups[2].Value] = @{ Type = ($m.Groups[1].Value -split '::')[-1]; Skip = @() }
}
foreach ($m in [regex]::Matches($enumCpp, '(?s)EnumMappings::(\w+)\(\)\s*\{(.*?)\n    \}')) {
    $body = $m.Groups[2].Value
    if ($body -match 'ConversionTrait<([^>]+)>::mappings') {
        $mapSource[$m.Groups[1].Value] = @{
            Type = ($Matches[1] -split '::')[-1]
            Skip = [regex]::Matches($body, 'enumStr\s*!=\s*"([^"]+)"') | ForEach-Object { $_.Groups[1].Value }
        }
    }
}

$checkedValues = 0
$uncheckedMaps = [System.Collections.Generic.List[string]]::new()
$initPattern = 'INITIALIZE_BINDABLE_ENUM_SETTING(?:_REVERSE_ORDER)?\(\s*\w+\s*,\s*(\w+)\s*,[^,]+,\s*L"([^"]+)"\s*,\s*L"([^"]+)"\s*\)'

foreach ($file in Get-ChildItem $EditorDir -Recurse -Include *.cpp -File | Where-Object { $_.FullName -notmatch '\\Generated Files\\' }) {
    foreach ($m in [regex]::Matches((Get-Content -Raw -LiteralPath $file.FullName), $initPattern)) {
        $mapName, $prefix, $property = $m.Groups[1].Value, $m.Groups[2].Value, $m.Groups[3].Value
        $src = $mapSource[$mapName]
        if (-not $src -or -not $keysForType.ContainsKey($src.Type)) {
            $uncheckedMaps.Add($mapName)
            continue
        }
        foreach ($key in $keysForType[$src.Type]) {
            if ($src.Skip -contains $key) { continue }
            # Utils.cpp uppercases the first letter; '/' in the lookup is the '.' in the resw.
            $expected = "$prefix$($key.Substring(0,1).ToUpperInvariant())$($key.Substring(1)).$property"
            $checkedValues++
            if ($editorStrings -notcontains $expected) {
                $problems.Add("enum dropdown '$mapName' ($($file.Name)) needs '$expected' in the editor's Resources.resw for value '$key' -- opening the page that owns it crashes the Settings Editor")
            }
        }
    }
}

if ($uncheckedMaps.Count -gt 0) {
    Write-Host ("Not checked (no json mapper found): {0}" -f (($uncheckedMaps | Sort-Object -Unique) -join ', ')) -ForegroundColor DarkYellow
}

Write-Host ("Checked {0} settings, {1} EnumMappings entries, {2} action arguments, {3} resource keys, {4} enum dropdown labels." -f $settings.Count, $declaredMaps.Count, $argNames.Count, $referencedKeys.Count, $checkedValues)

if ($problems.Count -gt 0) {
    Write-Host ''
    foreach ($p in $problems) { Write-Host "  FAIL: $p" -ForegroundColor Red }
    Write-Host ''
    exit 1
}

Write-Host 'No inconsistencies found.' -ForegroundColor Green
exit 0
