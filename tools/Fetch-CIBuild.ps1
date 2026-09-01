# Stage the newest CI-built Dev and Test payloads.
#
# The counterpart to Deploy-TerminalSlots.ps1 on a machine that should not be
# compiling Terminal. It ends in the same state a local deploy leaves behind --
# an unpacked payload on disk and a marker describing it, for each slot -- but
# in its own directories and under its own marker names, so both a local deploy
# and a CI fetch can have staged something at once and each consumer picks
# whichever is newer. Neither producer has to know about the other.
#
# It never touches a live payload. Registering either slot is a deliberate next
# step, not a side effect of staging: Promote-DevSlot.ps1 (Dev, from inside wtd)
# or Refresh-TestSlot.ps1 (Test, on demand) -- this just puts a candidate where
# each of those can find it.
#
# Safe to run on a timer: the common case is "nothing newer", which costs one API
# call and no download. See Install-CIBuildPoller.ps1.
[CmdletBinding()]
Param(
    [string]$Repo = 'steven-pribilinskiy/terminal',
    [string]$Workflow = 'build.yml',
    [string]$Branch = 'main',
    [string]$SlotRoot = 'C:\TerminalSlots',
    # Re-download even when the newest run is one we have already staged.
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$ArtifactName     = 'dev-payload'
$StageDir         = Join-Path $SlotRoot 'dev-staged-ci'
$MarkerPath       = Join-Path $SlotRoot 'dev-pending-ci.json'
$TestStageDir     = Join-Path $SlotRoot 'test-staged-ci'
$TestMarkerPath   = Join-Path $SlotRoot 'test-pending-ci.json'

function Say {
    Param([string]$Message, [ConsoleColor]$Color = [ConsoleColor]::Gray)
    Write-Host $Message -ForegroundColor $Color
}

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw 'gh is not on PATH. Install the GitHub CLI and run `gh auth login`.'
}

# --status success on purpose: a run that is still going has no artifact yet, and
# one that failed has nothing worth installing. Asking for the newest SUCCESSFUL
# run means a broken commit never displaces a working staged build.
$errFile = [System.IO.Path]::GetTempFileName()
$runJson = gh run list --repo $Repo --workflow $Workflow --branch $Branch `
    --status success --limit 1 --json databaseId,headSha,updatedAt 2>$errFile
$code = $LASTEXITCODE
$errText = (Get-Content $errFile -Raw -ErrorAction SilentlyContinue)
Remove-Item $errFile -Force -ErrorAction SilentlyContinue

if ($code -ne 0) {
    # Until the workflow has run once -- or while it exists only on a branch that
    # is not the default one -- gh exits non-zero saying it found no such
    # workflow. On a timer that is the steady state of a machine waiting for its
    # first CI build, not a fault, and throwing every quarter of an hour would
    # make a working setup look broken. Anything else (no auth, no network) still
    # fails loudly, because those do need attention.
    # gh reports a missing workflow and a missing REPO with byte-identical text
    # -- "workflow build.yml not found on the default branch" either way, with
    # only the URL differing -- so the message cannot be trusted to tell the
    # benign case from a typo'd repo or revoked auth. Prove the repo resolves
    # before calling it benign; otherwise a silent poller would report "no builds
    # yet" forever against a repo that does not exist.
    if ($errText -match 'not found on the default branch|could not find any workflows|no runs found') {
        gh repo view $Repo --json name > $null 2>&1
        if ($LASTEXITCODE -eq 0) {
            Say 'No CI builds yet.'
            return
        }
        throw "cannot reach $Repo -- check the repo name, and gh auth status"
    }
    throw "gh run list failed ($code): $errText"
}

$run = @($runJson | ConvertFrom-Json) | Select-Object -First 1
if (-not $run) {
    Say 'No successful run to fetch.'
    return
}

# The cheap exit, and the one that runs almost every time. Compare against what
# is already staged rather than what is running: re-downloading a payload we
# already have on disk is the waste worth avoiding, and whether it is worth
# PROMOTING is the Terminal's decision, not ours.
if (-not $Force -and (Test-Path $MarkerPath)) {
    $existing = $null
    try { $existing = Get-Content $MarkerPath -Raw | ConvertFrom-Json } catch { $existing = $null }
    # Also require test-staged-ci to exist: a marker written before this script
    # learned to keep the Test half would otherwise look "already staged" forever
    # and the Test payload would never get backfilled.
    if ($existing -and $existing.commitFull -eq $run.headSha -and (Test-Path $StageDir) -and (Test-Path $TestStageDir)) {
        Say "Already staged: $($run.headSha.Substring(0,9)). Nothing to do."
        return
    }
}

Say "Fetching run $($run.databaseId) ($($run.headSha.Substring(0,9)))" ([ConsoleColor]::Cyan)

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("wt-ci-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $temp | Out-Null
try {
    gh run download $run.databaseId --repo $Repo --name $ArtifactName --dir $temp 2>$null
    if ($LASTEXITCODE -ne 0) { throw "gh run download failed ($LASTEXITCODE) for run $($run.databaseId)" }

    $payloadSrc = Join-Path $temp 'dev-staged'
    $markerSrc  = Join-Path $temp 'dev-pending.json'
    if (-not (Test-Path (Join-Path $payloadSrc 'AppxManifest.xml'))) {
        throw "artifact has no AppxManifest.xml under dev-staged; not a usable payload"
    }

    # Replace wholesale rather than copying over: a file dropped from the package
    # would otherwise survive forever in the staged copy. Same reason
    # Deploy-TerminalSlots.ps1 unpacks its staged payload -Fresh.
    if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
    New-Item -ItemType Directory -Force -Path $SlotRoot | Out-Null
    Move-Item $payloadSrc $StageDir

    # The artifact's marker already carries the commit, branch and build time the
    # binaries were stamped with, so nothing is reconstructed here. Only the two
    # fields that are about THIS machine change: where the payload landed, and
    # which producer put it there.
    $info = Get-Content $markerSrc -Raw | ConvertFrom-Json
    $info | Add-Member -NotePropertyName payload -NotePropertyValue $StageDir -Force
    $info | Add-Member -NotePropertyName source -NotePropertyValue 'ci' -Force

    # Write beside the destination and rename into place: the Terminal watches
    # this folder and reacts to the write, so a half-written marker would be read
    # as malformed. A rename is atomic enough that it never sees a partial one.
    $markerTemp = "$MarkerPath.tmp"
    $info | ConvertTo-Json | Set-Content -Path $markerTemp -Encoding UTF8
    Move-Item $markerTemp $MarkerPath -Force

    # Same artifact carries a Test-branded unpacked payload too (build.yml stages
    # both from one compile). Nothing today asks for it automatically -- Refresh-
    # TestSlot.ps1 does that on demand -- but stage it every time regardless, same
    # as the Dev half: staging is cheap and automatic, registering never is.
    $testPayloadSrc = Join-Path $temp 'test-staged'
    if (Test-Path (Join-Path $testPayloadSrc 'AppxManifest.xml')) {
        if (Test-Path $TestStageDir) { Remove-Item -Recurse -Force $TestStageDir }
        Move-Item $testPayloadSrc $TestStageDir

        # No separate marker ships for the Test half; it is the same build as the
        # Dev marker just staged, so describe it with the same commit/branch/time.
        $testInfo = $info | Select-Object commit, commitFull, branch, dirty, timestampUtc, timestamp, source
        $testInfo | Add-Member -NotePropertyName payload -NotePropertyValue $TestStageDir -Force
        $testMarkerTemp = "$TestMarkerPath.tmp"
        $testInfo | ConvertTo-Json | Set-Content -Path $testMarkerTemp -Encoding UTF8
        Move-Item $testMarkerTemp $TestMarkerPath -Force
    }
    else {
        Say 'Artifact has no test-staged payload (older CI run, or build.yml changed) -- Test half not updated.' ([ConsoleColor]::DarkYellow)
    }

    # The Terminal looks for the promotion helper at exactly one fixed path, and
    # a machine that has only ever fetched CI builds has never run the deploy
    # that puts it there. Same reasoning for the on-demand Test-slot refresher.
    foreach ($helperName in 'Promote-DevSlot.ps1', 'Refresh-TestSlot.ps1', 'Invoke-Hidden.vbs') {
        $helper = Join-Path $SlotRoot $helperName
        if (-not (Test-Path $helper)) {
            Copy-Item (Join-Path $PSScriptRoot $helperName) $helper -Force
        }
    }

    Say "Staged $($info.commit) ($($info.branch)) built $($info.timestampUtc)" ([ConsoleColor]::Green)
    Say 'wtd will offer it as an update.'
    if (Test-Path $TestStageDir) {
        Say 'wtt: run Refresh-TestSlot.ps1 to pick it up.' ([ConsoleColor]::Green)
    }
}
finally {
    Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
}
