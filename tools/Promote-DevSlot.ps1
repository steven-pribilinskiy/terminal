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
# app only has to know one fixed path. Run it by hand if you prefer: with no
# -WaitForPid it waits for every process under the slot to exit, up to ten
# minutes, and then gives up. It never closes anything itself.
#
# Everything it does is announced on the console as well as written to the log.
# A promotion spends nearly all of its life waiting for windows that only the
# user can close, and a wait that prints nothing is indistinguishable from a
# hang -- which is exactly how it was first reported. When the Terminal spawns
# this detached there is no console at all, so nothing here may depend on the
# output being seen; the log remains the record.
[CmdletBinding()]
Param(
    # Wait for this process to exit before touching anything.
    [int]$WaitForPid = 0,
    # Start the Dev slot again once the swap is done.
    [switch]$Relaunch,
    [string]$Payload = 'C:\TerminalSlots\dev',
    # Where Deploy-TerminalSlots.ps1 leaves the build waiting. It cannot write
    # into $Payload: the running Dev instance holds its binaries open, so an
    # unpack over the live payload replaces the unlocked files and then fails.
    # The deploy therefore stages beside it and promotion moves it into place.
    [string]$Staged = 'C:\TerminalSlots\dev-staged'
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

# Console and log together, so the person watching and the log afterwards tell
# the same story. Write-Host is a no-op when there is no console, which is the
# detached case, so this is safe to call unconditionally.
function Say {
    Param([string]$Message, [ConsoleColor]$Color = [ConsoleColor]::Gray, [switch]$NoLog)
    Write-Host $Message -ForegroundColor $Color
    if (-not $NoLog -and $Message) { Write-Log $Message }
}

Write-Log "promotion requested (waitForPid=$WaitForPid relaunch=$($Relaunch.IsPresent) payload=$Payload)"

# Purely so the banner can name the build being promoted. A missing or broken
# marker is not a reason to refuse -- the payload on disk is what gets swapped,
# and the marker only describes it.
#
# Never name anything here $staged: PowerShell variable names are
# case-insensitive, so that assignment lands on the $Staged *path* parameter
# above and every later Join-Path against it fails with "Cannot find drive. A
# drive with the name '@{commit=...}' does not exist" -- which is how this
# silently refused to promote anything at all.
$markerInfo = $null
$markerPath = Join-Path (Split-Path -Parent $Payload) 'dev-pending.json'
if (Test-Path $markerPath) {
    try { $markerInfo = Get-Content $markerPath -Raw | ConvertFrom-Json } catch { $markerInfo = $null }
}

Write-Host ''
Say 'Promoting the staged build into the Dev slot' ([ConsoleColor]::Cyan) -NoLog
Say ("  payload : {0}" -f $Payload) -NoLog
if ($markerInfo) {
    Say ("  build   : {0}{1} ({2}), built {3}" -f `
        $markerInfo.commit, $(if ($markerInfo.dirty) { '+dirty' } else { '' }), $markerInfo.branch, $markerInfo.timestampUtc) -NoLog
}
Say ("  log     : {0}" -f $LogPath) -NoLog
Write-Host ''

try {
    $stagedManifest = Join-Path $Staged 'AppxManifest.xml'
    $haveStaged = Test-Path $stagedManifest
    if (-not $haveStaged -and -not (Test-Path $Manifest)) {
        throw "no payload to promote: neither $Staged nor $Payload has an AppxManifest.xml"
    }
    if ($haveStaged) { Say ("  staged  : {0}" -f $Staged) -NoLog }

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

    # "Apply on next launch" is the app spawning us against its own pid without
    # asking for a relaunch: it is not quitting, and the whole promise of that
    # button is that the swap happens whenever the user closes the build --
    # tonight, tomorrow, whenever. A ten minute deadline turns that promise into
    # a silent no-op for every session longer than ten minutes, which is nearly
    # all of them. So that request waits for as long as it takes.
    #
    # Every other shape keeps the deadline: "apply now" is quitting immediately
    # so ten minutes is generous, and a hand-run promotion has someone watching
    # a console who deserves an answer rather than a process that never returns.
    $deferred = ($WaitForPid -gt 0) -and (-not $Relaunch)
    $timeout  = [TimeSpan]::FromMinutes(10)
    $started  = Get-Date
    $deadline = if ($deferred) { $null } else { $started + $timeout }

    # Re-announced whenever the set changes rather than once at the top, so that
    # closing one window of several is visibly progress instead of silence.
    $announced = $null
    $ticking   = $false

    # A rewritten line is right in a console and wrong everywhere else: with the
    # output redirected the carriage returns do not rewrite anything and the
    # countdown piles up into a wall of near-identical copies. Measured, not
    # assumed -- it is what the first version of this did when captured.
    $rewrites = $false
    try { $rewrites = -not [Console]::IsOutputRedirected } catch { $rewrites = $false }
    $lastHeartbeat = [DateTime]::MinValue
    while ($true) {
        $live = @(Get-Process -Name 'WindowsTerminal' -ErrorAction SilentlyContinue |
                  Where-Object { $_.Path -and $_.Path.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase) })
        if ($live.Count -eq 0) { break }

        $now = Get-Date
        if ($deadline -and $now -gt $deadline) {
            if ($ticking) { Write-Host '' }
            Say ("Gave up: {0} process(es) still running after {1:N0} minutes (pids {2})." -f `
                $live.Count, $timeout.TotalMinutes, ($live.Id -join ',')) ([ConsoleColor]::Red)
            Say 'Nothing was changed. Close the Dev windows and run this again.' ([ConsoleColor]::Red) -NoLog
            exit 3
        }

        $key = (($live.Id | Sort-Object) -join ',')
        if ($key -ne $announced) {
            if ($ticking) { Write-Host ''; $ticking = $false }
            $announced = $key
            Say ("Waiting for {0} Dev process(es) to close before the registration can be swapped:" -f $live.Count) ([ConsoleColor]::Yellow)
            foreach ($l in $live) { Say ("    pid {0}  {1}" -f $l.Id, $l.Path) }
            Say  'Windows will not unregister a package that is still running, so this' ([ConsoleColor]::DarkGray) -NoLog
            Say  'cannot close them for you. Close your Dev Terminal windows and it' ([ConsoleColor]::DarkGray) -NoLog
            Say  'continues on its own.' ([ConsoleColor]::DarkGray) -NoLog
            if ($deadline) {
                Say ("Giving up at {0} if they are still open." -f $deadline.ToString('HH:mm:ss')) ([ConsoleColor]::DarkGray) -NoLog
            }
            else {
                Say 'This waits for as long as it takes; there is no deadline.' ([ConsoleColor]::DarkGray) -NoLog
            }
        }

        # One rewritten line rather than a scrolling wall, so a ten minute wait
        # still reads as one thing happening; a sparse heartbeat when redirected.
        $countdown = if ($deadline) { "{1:mm\:ss} until it gives up" } else { 'no deadline' }
        if ($rewrites) {
            Write-Host ("`r    waiting - {0:mm\:ss} elapsed, $countdown   " -f `
                ($now - $started), $(if ($deadline) { $deadline - $now } else { [TimeSpan]::Zero })) -NoNewline
            $ticking = $true
        }
        elseif (($now - $lastHeartbeat).TotalSeconds -ge 30) {
            $lastHeartbeat = $now
            Write-Host ("    still waiting - {0:mm\:ss} elapsed, $countdown" -f `
                ($now - $started), $(if ($deadline) { $deadline - $now } else { [TimeSpan]::Zero }))
        }

        Start-Sleep -Milliseconds 500
    }
    if ($ticking) { Write-Host '' }
    if ($announced) { Say 'All Dev processes have exited; continuing.' ([ConsoleColor]::Green) }

    # Belt and braces. Remove-AppxPackage -PreserveApplicationData keeps
    # LocalState, but this slot's settings are the whole point of it having a
    # separate identity, so back them up before touching the registration.
    $localState = Join-Path $env:LOCALAPPDATA "Packages\$FamilyName\LocalState\settings.json"
    $backup = $null
    if (Test-Path $localState) {
        $backup = Join-Path $env:TEMP "wtd-settings-$([DateTime]::UtcNow.ToString('yyyyMMddHHmmss')).json"
        Copy-Item $localState $backup -Force
        Say ("Backed up the Dev slot's settings.json to {0}" -f $backup)
    }

    Say ("Removing the old registration at {0}" -f $installRoot)
    Remove-AppxPackage -Package $pkg.PackageFullName -PreserveApplicationData

    # Only now is the swap safe: nothing is running out of the payload and
    # nothing is registered from it. Two renames on the same volume, so the
    # window where the slot has no payload at all is a few milliseconds, and the
    # outgoing build is kept until the new registration has been asserted -- if
    # anything below fails, the previous payload is still on disk to go back to.
    $previous = "$Payload.previous"
    if ($haveStaged) {
        if (Test-Path $previous) { Remove-Item -Recurse -Force $previous }
        if (Test-Path $Payload) {
            Move-Item -LiteralPath $Payload -Destination $previous -Force
            Write-Log "moved the outgoing payload to $previous"
        }
        Say ("Swapping in the staged build from {0}" -f $Staged)
        Move-Item -LiteralPath $Staged -Destination $Payload -Force
    }

    Say ("Registering the Dev slot from {0}" -f $Manifest)
    Add-AppxPackage -Path $Manifest -Register

    # -Register is a silent no-op when the identity is still registered
    # elsewhere, so never trust it; assert where we actually landed.
    $landed = (Get-AppxPackage -Name $PackageName).InstallLocation
    if ($landed -ne $Payload) { throw "registration landed at '$landed', expected '$Payload'" }

    if ($haveStaged -and (Test-Path $previous)) {
        Remove-Item -Recurse -Force $previous -ErrorAction SilentlyContinue
    }
    Say ("Promoted: {0} now runs from {1}" -f $PackageName, $Payload) ([ConsoleColor]::Green)

    if ($backup -and -not (Test-Path $localState)) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $localState) | Out-Null
        Copy-Item $backup $localState -Force
        Say 'Restored the Dev slot settings.json'
    }

    # The marker described the payload we just promoted; it is no longer
    # pending, and leaving it would keep the button lit in the new build.
    $pending = Join-Path (Split-Path -Parent $Payload) 'dev-pending.json'
    if (Test-Path $pending) { Remove-Item $pending -Force }

    if ($Relaunch) {
        Say 'Relaunching the Dev slot (wtd.exe)'
        Start-Process 'wtd.exe'
    }
    else {
        Say 'Start it yourself with: wtd' ([ConsoleColor]::DarkGray) -NoLog
    }

    Write-Host ''
    Say 'Promotion complete.' ([ConsoleColor]::Green) -NoLog
    Write-Host ''
    exit 0
}
catch {
    Write-Log "FAILED: $($_.Exception.Message)"
    Write-Host ''
    Write-Host "Promotion failed: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Nothing was swapped. See $LogPath" -ForegroundColor Red
    Write-Host ''
    exit 1
}
