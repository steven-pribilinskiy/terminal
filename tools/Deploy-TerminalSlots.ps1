# Builds and deploys the two local Terminal slots.
#
#   Test slot (wtt.exe) -- disposable. Registered immediately; replace and
#                          restart it as often as you like.
#   Dev slot  (wtd.exe) -- where real work happens. NOT touched here. Its
#                          payload is staged so the running Dev instance can
#                          offer to promote it when you have no active
#                          sessions, which is the only moment it is safe to
#                          restart.
#
# Both packages are produced from the SAME compiled binaries: Branding.targets
# maps an unrecognised branding to WT_BRANDING_DEV, so a Test build differs from
# a Dev build only in its manifest. What you verify in wtt is byte-for-byte the
# code that gets promoted into wtd.
#
# That is a property this repo has to actively maintain, not one it gets for
# free. The two packaging passes below pass WindowsTerminalBranding on the
# command line, which makes it a GLOBAL property, and MSBuild forwards global
# properties to every ProjectReference -- so each pass genuinely recompiles the
# C++ into the shared bin\ directory. Any build file that special-cases the
# branding therefore has to list 'Test' wherever it lists 'Dev', or the two
# passes quietly produce different binaries. It has happened: src\
# common.build.post.props and src\host\proxy\Host.Proxy.vcxproj both omitted
# 'Test' and the staged exes differed by 2,560 bytes. The check below is cheap;
# run it if you touch anything branding-conditional.
#
#   (Get-FileHash C:\TerminalSlots\dev\WindowsTerminal.exe).Hash -eq
#   (Get-FileHash C:\TerminalSlots\test\WindowsTerminal.exe).Hash
[CmdletBinding()]
Param(
    # Skip the solution build and just repackage/deploy what is already built.
    [switch]$NoBuild,
    # Stage the Dev payload but do not register the Test slot either.
    [switch]$StageOnly
)

$ErrorActionPreference = 'Stop'

$Root      = Split-Path -Parent $PSScriptRoot
$SlotRoot  = 'C:\TerminalSlots'
$TestStage = Join-Path $SlotRoot 'test'
$DevStage  = Join-Path $SlotRoot 'dev'
$Config    = 'Release'
$Platform  = 'x64'

$makeappx = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\makeappx.exe' -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $makeappx) { throw 'makeappx.exe not found; is the Windows SDK installed?' }

# Don't depend on the caller having already set up the dev environment: without it
# msbuild.exe simply isn't on PATH and the script dies halfway through packaging.
if (-not (Get-Command msbuild.exe -ErrorAction SilentlyContinue)) {
    Write-Host '== setting up MSBuild dev environment ==' -ForegroundColor DarkGray
    Import-Module "$Root\tools\OpenConsole.psm1" -Force
    Set-MsbuildDevEnvironment
    if (-not (Get-Command msbuild.exe -ErrorAction SilentlyContinue)) {
        throw 'msbuild.exe still not on PATH after Set-MsbuildDevEnvironment'
    }
}

function Invoke-MsBuild {
    Param([string[]]$Arguments)
    & msbuild.exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "msbuild failed ($LASTEXITCODE): $($Arguments -join ' ')" }
}

if (-not $NoBuild) {
    Write-Host '== building solution ==' -ForegroundColor Cyan
    Invoke-MsBuild @("$Root\OpenConsole.slnx", "/p:Configuration=$Config", "/p:Platform=$Platform", '/m', '/v:m', '/nologo')
}

# The wapproj is built directly for each branding, so SolutionDir has to be passed
# explicitly -- it is only set automatically when building through the .slnx.
$wapproj = "$Root\src\cascadia\CascadiaPackage\CascadiaPackage.wapproj"

# The packaging project caches its generated AppxManifest in obj\. Building two
# brandings back to back can otherwise reuse the previous branding's manifest
# against the current branding's alias stub, and MakeAppx rejects the mismatch
# ("the file name wtt.exe ... doesn't exist in the package"). Clearing the
# packaging intermediates is cheap; the C++ underneath does get revisited, but
# with the branding conditions kept in sync it compiles to the same bytes and
# so mostly comes back from the up-to-date check.
$pkgObj = "$Root\src\cascadia\CascadiaPackage\obj"
if (Test-Path $pkgObj) {
    Write-Host "== clearing packaging intermediates ==" -ForegroundColor DarkGray
    Remove-Item -Recurse -Force $pkgObj -ErrorAction SilentlyContinue
}

Write-Host '== packaging Test slot ==' -ForegroundColor Cyan
Invoke-MsBuild @($wapproj, "/p:Configuration=$Config", "/p:Platform=$Platform",
                 '/p:WindowsTerminalBranding=Test', "/p:SolutionDir=$Root\", '/m', '/v:m', '/nologo')

if (Test-Path $pkgObj) { Remove-Item -Recurse -Force $pkgObj -ErrorAction SilentlyContinue }

Write-Host '== packaging Dev slot (staged, not installed) ==' -ForegroundColor Cyan
Invoke-MsBuild @($wapproj, "/p:Configuration=$Config", "/p:Platform=$Platform",
                 '/p:WindowsTerminalBranding=Dev', "/p:SolutionDir=$Root\", '/m', '/v:m', '/nologo')

function Expand-Slot {
    Param([string]$MsixDir, [string]$Destination, [string]$Label)
    $msix = Get-ChildItem $MsixDir -Filter *.msix -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $msix) { throw "no .msix produced for $Label in $MsixDir" }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    & $makeappx unpack /o /p $msix.FullName /d $Destination | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "makeappx unpack failed for $Label" }
    return $msix
}

$pkgBase  = "$Root\src\cascadia\CascadiaPackage\AppPackages"
$testMsix = Expand-Slot -MsixDir "$pkgBase\Test\CascadiaPackage_0.0.1.0_${Platform}_Test" -Destination $TestStage -Label 'Test'
$devMsix  = Expand-Slot -MsixDir "$pkgBase\CascadiaPackage_0.0.1.0_${Platform}_Test"      -Destination $DevStage  -Label 'Dev'

# The running Dev instance reads this to decide whether a newer build is waiting.
# It sits beside the payload rather than inside it, so staging needs no changes
# to the packaging itself.
$header = Get-Content "$Root\bin\$Config\inc\TerminalBuildInfo.h" -Raw
function Get-Define {
    Param([string]$Name)
    if ($header -match "#define\s+$Name\s+L?`"([^`"]*)`"") { return $Matches[1] }
    if ($header -match "#define\s+$Name\s+([0-9]+)") { return $Matches[1] }
    return 'unknown'
}

$info = [ordered]@{
    commit       = Get-Define 'TERMINAL_BUILD_COMMIT'
    commitFull   = Get-Define 'TERMINAL_BUILD_COMMIT_FULL'
    branch       = Get-Define 'TERMINAL_BUILD_BRANCH'
    dirty        = [int](Get-Define 'TERMINAL_BUILD_DIRTY')
    timestamp    = [int64](Get-Define 'TERMINAL_BUILD_TIMESTAMP')
    timestampUtc = Get-Define 'TERMINAL_BUILD_TIMESTAMP_STRING'
    payload      = $DevStage
}
$infoPath = Join-Path $SlotRoot 'dev-pending.json'
$info | ConvertTo-Json | Set-Content -Path $infoPath -Encoding UTF8

# The promote button in a running Dev window shells out to this. It lives beside
# the payloads rather than in the repo so the app has exactly one fixed path to
# know, and so promotion still works from a checkout that has moved.
Copy-Item "$PSScriptRoot\Promote-DevSlot.ps1" (Join-Path $SlotRoot 'Promote-DevSlot.ps1') -Force

if (-not $StageOnly) {
    Write-Host '== registering Test slot ==' -ForegroundColor Cyan

    # Always unregister first. Two separate failures make this non-optional:
    #   * re-registering the same version from a DIFFERENT folder is a silent no-op
    #     -- it reports success and leaves the slot running the previous build;
    #   * re-registering the same version from the SAME folder after the files
    #     changed fails outright with 0x80073CFB ("the package is already
    #     installed, and reinstallation was blocked ... in development mode").
    # doc/building.md removes the package first for the same reason.
    $existing = Get-AppxPackage -Name 'WindowsTerminalTest' -ErrorAction SilentlyContinue

    # Removing a package deletes its app data, and the Test slot's settings.json is
    # worth keeping across deploys -- it is where the slot gets configured to look
    # like anything other than a bare default profile.
    $testLocalState = "$env:LOCALAPPDATA\Packages\WindowsTerminalTest_8wekyb3d8bbwe\LocalState\settings.json"
    $savedSettings = $null
    if (Test-Path $testLocalState) {
        $savedSettings = Get-Content $testLocalState -Raw -ErrorAction SilentlyContinue
    }

    if ($existing) {
        Write-Host "   unregistering $($existing.PackageFullName)" -ForegroundColor DarkGray
        Remove-AppxPackage -Package $existing.PackageFullName -ErrorAction Stop
    }

    Add-AppxPackage -Path (Join-Path $TestStage 'AppxManifest.xml') -Register -ForceUpdateFromAnyVersion

    $now = Get-AppxPackage -Name 'WindowsTerminalTest' -ErrorAction SilentlyContinue
    if (-not $now -or (Resolve-Path $now.InstallLocation).Path -ne (Resolve-Path $TestStage).Path) {
        throw "Test slot did not register from $TestStage (still: $($now.InstallLocation))"
    }

    if ($savedSettings) {
        $dir = Split-Path -Parent $testLocalState
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        Set-Content -Path $testLocalState -Value $savedSettings -Encoding UTF8
        Write-Host '   restored Test slot settings.json' -ForegroundColor DarkGray
    }
}

Write-Host ''
Write-Host "Test slot  : registered from $TestStage (run: wtt)" -ForegroundColor Green
Write-Host "Dev slot   : staged at $DevStage -- NOT installed" -ForegroundColor Yellow
Write-Host "Pending    : $infoPath" -ForegroundColor Yellow
Write-Host "Build      : $($info.commit) on $($info.branch), built $($info.timestampUtc)"
Write-Host ''
Write-Host 'The Dev slot is intentionally left alone. Promote it from the About' -ForegroundColor DarkGray
Write-Host 'dialog in wtd when you have no active sessions.' -ForegroundColor DarkGray
