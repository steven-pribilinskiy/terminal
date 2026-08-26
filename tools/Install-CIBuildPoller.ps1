# Register a Scheduled Task that keeps the newest CI build staged for wtd.
#
# For the notebook, where builds happen in CI rather than locally. Without it the
# UPDATE button only appears after remembering to run Fetch-CIBuild.ps1 by hand,
# which is the kind of thing you remember about as often as you need it.
#
# Staging stays automatic; promoting stays a gesture. This never installs
# anything -- it puts a candidate on disk and lets the running Terminal offer it.
#
#   pwsh -File .\tools\Install-CIBuildPoller.ps1
#   pwsh -File .\tools\Install-CIBuildPoller.ps1 -Uninstall
[CmdletBinding()]
Param(
    [int]$IntervalMinutes = 15,
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'

$TaskName = 'Terminal CI build poller'
$Fetcher  = Join-Path $PSScriptRoot 'Fetch-CIBuild.ps1'

if ($Uninstall) {
    if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
        Write-Host "Removed '$TaskName'." -ForegroundColor Green
    }
    else {
        Write-Host "'$TaskName' is not registered." -ForegroundColor DarkGray
    }
    return
}

if (-not (Test-Path $Fetcher)) { throw "Fetch-CIBuild.ps1 not found beside this script" }

$pwsh = (Get-Command pwsh -ErrorAction SilentlyContinue)?.Source
if (-not $pwsh) { throw 'pwsh not found on PATH. The fetcher needs PowerShell 7.' }

$action = New-ScheduledTaskAction -Execute $pwsh `
    -Argument "-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$Fetcher`""

# Repeat forever from a start in the past, so it begins on the next interval
# rather than waiting for a first occurrence.
$trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(-1) `
    -RepetitionInterval (New-TimeSpan -Minutes $IntervalMinutes)

# The battery settings are the point of a notebook-specific task. The defaults
# refuse to start on battery AND stop a running task when the machine unplugs,
# which for a 17 MB download is more disruption than the download is worth
# avoiding -- but running a poll every quarter hour on a train is worse. Keep the
# "don't start on battery" default and drop the "kill it mid-flight" one, so a
# fetch that began on mains is allowed to finish.
$settings = New-ScheduledTaskSettingsSet `
    -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -MultipleInstances IgnoreNew `
    -ExecutionTimeLimit (New-TimeSpan -Minutes 10)

Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
    -Settings $settings -Description 'Downloads the newest successful CI build and stages it for the Dev slot.' `
    -Force | Out-Null

Write-Host "Registered '$TaskName' every $IntervalMinutes minutes." -ForegroundColor Green
Write-Host "  fetcher : $Fetcher" -ForegroundColor DarkGray
Write-Host "  run now : Start-ScheduledTask -TaskName '$TaskName'" -ForegroundColor DarkGray
