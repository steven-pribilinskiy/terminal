# Promotes the staged payload into the Dev slot, once the Dev slot is no longer
# running.
#
# This is the half of the promotion that cannot happen inside the Terminal:
# re-registering a package identity from a different folder means removing the
# old registration first, and Windows will not remove a running package. The
# Terminal therefore spawns this detached, with its own process id, and exits;
# we wait for it to go, then swap.
#
# Deploy-TerminalSlots.ps1 copies this next to the payloads it stages, so the
# app only has to know one fixed path. Run it by hand if you prefer -- with no
# -WaitForPid it simply refuses while anything is running under the slot, the
# same as Register-DevSlot.ps1.
[CmdletBinding()]
Param(
    # Wait for this process to exit before touching anything.
    [int]$WaitForPid = 0,
    # Start the Dev slot again once the swap is done.
    [switch]$Relaunch,
    [string]$Payload = 'C:\TerminalSlots\dev'
)

$ErrorActionPreference = 'Stop'

$PackageName = 'WindowsTerminalDev'
$FamilyName  = 'WindowsTerminalDev_8wekyb3d8bbwe'
$Manifest    = Join-Path $Payload 'AppxManifest.xml'
$LogPath     = Join-Path (Split-Path -Parent $Payload) 'promote-dev.log'

function Write-Log {
    Param([string]$Message)
    "$([DateTime]::UtcNow.ToString('u')) $Message" | Add-Content -Path $LogPath -Encoding UTF8
}

Write-Log "promotion requested (waitForPid=$WaitForPid relaunch=$($Relaunch.IsPresent) payload=$Payload)"

try {
    if (-not (Test-Path $Manifest)) { throw "no staged payload at $Payload" }

    # Wait for the process that asked for this, then for anything else still
    # running under the slot -- other windows may outlive the one that asked.
    if ($WaitForPid -gt 0) {
        try {
            $p = Get-Process -Id $WaitForPid -ErrorAction Stop
            Write-Log "waiting for pid $WaitForPid"
            $p.WaitForExit()
        } catch [Microsoft.PowerShell.Commands.ProcessCommandException] {
            Write-Log "pid $WaitForPid already gone"
        }
    }

    $pkg = Get-AppxPackage -Name $PackageName
    if (-not $pkg) { throw "$PackageName is not registered" }

    # Match on PATH, not process name: every slot and the real install all run
    # an executable called WindowsTerminal.exe.
    $installRoot = $pkg.InstallLocation
    $deadline = (Get-Date).AddMinutes(10)
    while ($true) {
        $live = @(Get-Process -Name 'WindowsTerminal' -ErrorAction SilentlyContinue |
                  Where-Object { $_.Path -and $_.Path.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase) })
        if ($live.Count -eq 0) { break }
        if ((Get-Date) -gt $deadline) {
            Write-Log "giving up: still running after 10 minutes (pids $($live.Id -join ','))"
            exit 3
        }
        Start-Sleep -Milliseconds 500
    }

    # Belt and braces. Remove-AppxPackage -PreserveApplicationData keeps
    # LocalState, but this slot's settings are the whole point of it having a
    # separate identity, so back them up before touching the registration.
    $localState = Join-Path $env:LOCALAPPDATA "Packages\$FamilyName\LocalState\settings.json"
    $backup = $null
    if (Test-Path $localState) {
        $backup = Join-Path $env:TEMP "wtd-settings-$([DateTime]::UtcNow.ToString('yyyyMMddHHmmss')).json"
        Copy-Item $localState $backup -Force
        Write-Log "settings backed up to $backup"
    }

    Write-Log "removing registration at $installRoot"
    Remove-AppxPackage -Package $pkg.PackageFullName -PreserveApplicationData

    Write-Log "registering from $Manifest"
    Add-AppxPackage -Path $Manifest -Register

    # -Register is a silent no-op when the identity is still registered
    # elsewhere, so never trust it; assert where we actually landed.
    $now = (Get-AppxPackage -Name $PackageName).InstallLocation
    if ($now -ne $Payload) { throw "registration landed at '$now', expected '$Payload'" }
    Write-Log "promoted: $PackageName now runs from $Payload"

    if ($backup -and -not (Test-Path $localState)) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $localState) | Out-Null
        Copy-Item $backup $localState -Force
        Write-Log 'settings restored'
    }

    # The marker described the payload we just promoted; it is no longer
    # pending, and leaving it would keep the button lit in the new build.
    $pending = Join-Path (Split-Path -Parent $Payload) 'dev-pending.json'
    if (Test-Path $pending) { Remove-Item $pending -Force }

    if ($Relaunch) {
        Write-Log 'relaunching'
        Start-Process 'wtd.exe'
    }
    exit 0
}
catch {
    Write-Log "FAILED: $($_.Exception.Message)"
    exit 1
}
