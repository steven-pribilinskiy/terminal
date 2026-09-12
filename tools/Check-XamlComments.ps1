<#
Copyright (c) Microsoft Corporation.
Licensed under the MIT license.
.SYNOPSIS
Rejects XAML comments that the XAML compiler will not parse.

.DESCRIPTION
XML forbids "--" inside a comment, and forbids a comment body ending in "-".
The XAML compiler enforces both and fails the build with

    Xaml Xml Parsing Error error WMC9997: An XML comment cannot contain '--',
    and '-' cannot be the last character.

which arrives about fifteen minutes into "Restore and build" -- so the whole
~40 minute round trip is spent to learn that a comment had a dash in it.

The reason this needs its own check is that the obvious local validation does
NOT catch it. .NET's XmlDocument is lenient here:

    [xml](Get-Content -Raw file.xaml)   # says the file is fine

reports success on a file the XAML compiler rejects. That false pass is what
made this worth a gate rather than a note. It cost a build on 2026-09-12, on a
comment that used this repo's own "--" prose style inside markup.

Prose in a XAML comment should use a real em dash, a comma, or a full stop.

.EXAMPLE
pwsh -File tools\Check-XamlComments.ps1
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)][string]$Root
)

$ErrorActionPreference = 'Stop'

# Resolve in the body, not in a param default: under Windows PowerShell 5.1
# $PSScriptRoot is empty while parameter defaults are evaluated. See
# Check-SettingsModelConsistency.ps1, which this bit their gate first.
if (-not $Root) { $Root = Join-Path (Split-Path -Parent $PSScriptRoot) 'src' }

$problems = [System.Collections.Generic.List[string]]::new()
$checked = 0

foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File -Filter *.xaml) {
    # Generated output is not ours to fix and is not compiled from source here.
    if ($file.FullName -like '*\Generated Files\*') { continue }

    $checked++
    $text = Get-Content -Raw -LiteralPath $file.FullName

    foreach ($m in [regex]::Matches($text, '(?s)<!--(.*?)-->')) {
        $body = $m.Groups[1].Value
        $line = ($text.Substring(0, $m.Index) -split "`n").Count

        if ($body.Contains('--')) {
            $problems.Add("$($file.FullName):$line  comment contains '--', which XML forbids; use an em dash or rephrase")
        }
        if ($body.EndsWith('-')) {
            $problems.Add("$($file.FullName):$line  comment body ends with '-', which XML forbids")
        }
    }
}

Write-Host ("Checked {0} XAML files." -f $checked)

if ($problems.Count -gt 0) {
    Write-Host ''
    foreach ($p in $problems) { Write-Host "  FAIL: $p" -ForegroundColor Red }
    Write-Host ''
    exit 1
}

Write-Host 'No unparseable XAML comments.' -ForegroundColor Green
exit 0
