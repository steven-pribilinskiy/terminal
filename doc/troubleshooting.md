# Troubleshooting a locally built Terminal

Failures specific to *this* fork's two-slot dev loop, written down because each
one cost real time and none of them says what it means. See `CLAUDE.md` for the
slot doctrine itself; this file is only about diagnosing things that break.

## A slot starts and vanishes without ever showing a window

What you see: `wtt` or `wtd` "launches", no window appears, and the process is
gone in a couple of seconds. `Get-WinEvent -LogName Application` (event ID 1000)
reports something like:

```
Faulting application path: C:\TerminalSlots\dev\WindowsTerminal.exe
Faulting module name:      ucrtbase.dll
Exception code:            0xc0000409
Exception Data (P10):      0000000000000007
```

**Read the exception data, not just the code.** `0xC0000409` is reported as
"stack buffer overrun", which it usually is not. The trailing parameter is the
`__fastfail` reason, and `7` is `FAST_FAIL_FATAL_APP_EXIT` — plain `abort()`.
Other values mean genuinely different things (`2` really is a stack cookie
failure, `5` is an invalid argument from the CRT). Faulting module `ucrtbase.dll`
just means `abort()` lives there; it says nothing about who called it.

### The cause seen on 2026-08-27

An **incremental build with stale generated artifacts**. Commit `8fef97d22`
changed `TabRowControl.idl`/`.xaml` and `AboutDialog.idl`/`.xaml`, but msbuild
considered several dependent DLLs up to date, so the deployed payload mixed
newly generated interfaces with DLLs built ~19 hours earlier. The result aborted
inside `AppHost::Initialize`, before any window existed.

The fix was a **full solution build**. Two incremental deploys did not fix it;
the full build did, and copying only those consistently built binaries into the
slot — same package, same settings, nothing else changed — made it start.

The precise stale artifact was never isolated to a single file, so treat this as
the rule rather than a story about one DLL:

> After changing a `.idl` or a `.xaml`, do not trust an incremental build.
> Build the whole solution before deploying, or verify the result actually
> starts.

`tools\Deploy-TerminalSlots.ps1` now enforces the second half: it starts the
registered Test slot and waits for a window before writing `dev-pending.json`,
so a build that cannot start can no longer be staged for promotion into the slot
that hosts live sessions. If it fails, Test stays registered so `wtt` reproduces
it for you.

## The Dev slot went *backwards* after pressing promote

What you see: you promote, `wtd` relaunches, and the tab row or About dialog now
names an older commit than the one you were running.

`C:\TerminalSlots\promote-dev.log` is the record and it names the payload, so
start there:

```
08:41:57Z promotion requested (waitForPid=38916 relaunch=True payload=C:\TerminalSlots\dev)
08:41:59Z Swapping in the staged build from C:\TerminalSlots\dev-staged-ci
```

`waitForPid` set with `relaunch=True` is the in-app button; `waitForPid=0` is a
hand-run promotion.

### Why it happened on 2026-08-28

`SlotPromotion::DiffersFromRunning()` returned `true` for **any** commit
mismatch, before it ever reached its timestamp comparison. A different commit is
not evidence of being newer — it is just as easily older. A stale
`dev-pending-ci.json` describing an Aug-26 CI build was therefore offered to a
window running a newer local build, and promoting it installed the older one
over the newer. The function's own comment claimed an older marker "never
advertises itself as new", which the code did not implement.

It is now `IsNewerThanRunning()`, and build time is checked first and
unconditionally: a payload built before the running build is never offered,
whatever commit it names.

### Checking what is on offer

Every `dev-pending*.json` in `C:\TerminalSlots` is a candidate — one per
producer (`dev-pending.json` from a local deploy, `dev-pending-ci.json` from
`Fetch-CIBuild.ps1`), by design, because the producers run on different machines
and know nothing about each other. Compare each marker's `timestampUtc` against
the running build before promoting:

```powershell
Get-ChildItem C:\TerminalSlots -Filter 'dev-pending*.json' |
    ForEach-Object { $_.Name; Get-Content $_.FullName | ConvertFrom-Json |
                     Select-Object commit, branch, timestampUtc, payload }
```

A marker describing a build older than the one you are running is stale.
Promotion consumes only the marker it used, so another producer's marker
survives on purpose — it may still describe something genuinely newer.

## Diagnosing a startup abort without a debugger

There is no `cdb.exe`/WinDbg on this machine, and WER keeps only `Report.wer`
(no minidump) in `ReportArchive`. What works instead is making the process
report on itself. Add this temporarily to
`src\cascadia\WindowsTerminal\main.cpp`, build **only** that project, and revert
it afterwards — it is a diagnostic, never something to commit.

Three pieces, and you want all three:

- **A vectored exception handler** (`AddVectoredExceptionHandler(1, ...)`),
  filtering on `0xE06D7363` (the MSVC C++ throw code). This is process-wide and
  fires on *every* thread, before unwinding. It is noisy by design — Terminal
  throws and catches routinely — so read the last entry.
- **A `SIGABRT` handler** plus
  `_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT)`. `abort()`
  bypasses SEH and VEH entirely, so hooking the signal is the only way to see
  *who called it*. This is the one that actually located the fault.
- **`std::set_terminate`** — useful, but see the trap below: it is per-thread in
  MSVC, so it silently does nothing if the failure is on a UI thread.

Symbolize in-process with dbghelp (`SymInitialize` + `SymFromAddr` over
`RtlCaptureStackBackTrace`); `#pragma comment(lib, "dbghelp.lib")`. The PDBs sit
next to the built binaries and the path is embedded in the image, so frames
resolve with no extra setup — as long as the binary and its PDB match.

## Tests that lie

Every one of these produced a confident, wrong conclusion.

**Launching a slot payload exe directly.** `C:\TerminalSlots\test\WindowsTerminal.exe`
started from PowerShell gets **no package identity**, so activating the
`TerminalApp.App` WinRT class fails with `0x80040154 REGDB_E_CLASSNOTREG` and
the process aborts — *for a perfectly healthy build*. Launch through the alias
(`wtt`) or the AUMID (`shell:AppsFolder\WindowsTerminalTest_8wekyb3d8bbwe!App`).
The alias is a stub that exits immediately, so find the real process by payload
path, not by the PID you started. A run with no identity is visible after the
fact: event 1000 shows an empty `Faulting package full name`.

**"I rebuilt and it still crashes."** Check that the rebuild actually produced a
new binary. An incremental build can decide the exe is up to date and never
relink it, so you have tested nothing. Compare the PE timestamp
(`Sig[3] Application Timestamp` in WER, or the `Application Timestamp` field of
event 1000) across runs — not the file's `LastWriteTime`, which `makeappx
unpack` rewrites for every file in the payload anyway.

**Frames that resolve to a random export.** A stack full of
`DllGetActivationFactory + 0x19e93` and `DllCanUnloadNow + 0x4c2f1` does not
mean the code is anywhere near activation. It means dbghelp had no symbols for
that module and fell back to the nearest preceding export. Usually the DLL in
the slot is older than the PDB next to the rebuilt one. Copy the freshly built
DLLs into the payload and the names appear.

**Pipeline exit codes.** `& msbuild.exe ... | Select-String ...` leaves
`$LASTEXITCODE` set by `Select-String`, so a failed build reports success. This
is the same trap `CLAUDE.md` records for `tail`. Redirect to a file, read
`$LASTEXITCODE` on the very next line, then grep the file.

**`api-ms-win-*.dll` "missing".** Those are API sets resolved by the loader, not
files on disk. A naive existence check reports dozens of missing dependencies
for a perfectly loadable DLL. Likewise, `LoadLibraryW` on a payload DLL fails
with error 126 purely because the caller's directory is not on the search path —
use `LoadLibraryExW(..., LOAD_WITH_ALTERED_SEARCH_PATH)`.

**Checking out `upstream/main` in the working tree.** It regenerates the WinRT
projections, so coming back to `main` leaves `Microsoft.Terminal.Settings.Model.winmd`
without fork-only members and the next build fails with
`error C2039: 'OpenLinksOnSingleClick': is not a member of ...`. Recover with a
full solution build. Bisect in a separate worktree at a short path instead (see
`CLAUDE.md` on `MAX_PATH` and on WSL and `git worktree prune`).

## Builds that die on memory, not on code

```
error C3859: Failed to create virtual memory for PCH
error C1076: compiler limit: internal heap limit reached
```

This is the commit limit, not a disk or code problem. Check
`Get-Counter '\Memory\Committed Bytes','\Memory\Commit Limit'` rather than free
RAM: a large resident WSL VM (measured at 84 GB here) can leave ~46 GB of commit
against a 209 GB limit while RAM looks fine.

**`/m` alone is not enough.** It bounds how many *projects* build at once, while
`MultiProcessorCompilation` independently forks one `cl.exe` per core *inside*
each project. On this 32-core machine a full rebuild died at both `/m` and
`/m:4`, and survived at `/m:2 /p:CL_MPCount=2`. `Deploy-TerminalSlots.ps1
-MaxCpuCount N` now caps both.

## Verifying without taking the keyboard

A startup check has to launch a GUI app, which normally steals focus. To test
while the machine is in use, launch onto a **separate desktop**: `CreateDesktop`,
then `CreateProcess` with `STARTUPINFO.lpDesktop` set to it. Windows created
there cannot take focus, and `EnumDesktopWindows` still tells you whether one
appeared (`Process.MainWindowHandle` will not — it only enumerates the calling
thread's desktop). Read the exit code from the process handle you own via
`GetExitCodeProcess`; `Process.ExitCode` on a process .NET did not start throws
and is easily mistaken for a crash code.

Note the limitation that made this insufficient on its own here: a
separate-desktop launch is still an *unpackaged* launch, so it hits the
identity trap above. It is a good way to smoke-test a binary, not a substitute
for starting the registered slot.

## Local build traps, retained for reference

Builds happen in CI now (see the build section of `CLAUDE.md`), so none of these
should bite in day-to-day work. They are kept because they each cost a real
investigation, and because a one-off local build — bisecting, or reproducing
something CI cannot — will meet them again.

### `LNK1104: cannot open file ...` on a file that is plainly there

It means "path too long", not "missing". Some package references expand through
unnormalised `build\native\..\..\runtimes\...` segments, which pushes them past
`MAX_PATH` (260). A worktree under `%TEMP%\claude\<session>\scratchpad\`
produced a 267-character reference and failed to link; the same worktree at
`C:\wt-tm` produced 155 and linked. MSBuild also warns `MSB8029` about output
directories under Temp. Keep any build tree on a short root.

### `Invoke-OpenConsoleBuild -p:Configuration=Release` silently loses the switches

PowerShell parses `-p:Configuration=Release` as the cmdlet's own `-p:` parameter
with the value `Configuration=Release`, strips the prefix, and MSBuild receives a
bare `Configuration=Release` — then dies with `MSB1008: Only one project can be
specified`. Call `msbuild.exe` directly with `/p:` switches, or pass `--%` to
stop PowerShell parsing the rest of the line.

### Building a single project needs `SolutionDir`

`.vcxproj` files here import `$(SolutionDir)src\common.build.pre.props`, which is
empty when msbuild is pointed at a project rather than the solution:
`MSB4019: The imported project "...\src\common.build.pre.props" was not found`.
Pass it explicitly, with the trailing separator:
`msbuild .\src\types\lib\types.vcxproj /p:SolutionDir=C:\Users\steve\projects\terminal\`.

### A build tree is expensive

A warm tree here measured 40.7 GB: 33.9 GB across the eight `obj\` directories,
5.5 GB in `bin\`, plus `packages\` and `AppPackages\`. `NuGet.Config` pins
`globalPackagesFolder` and `repositoryPath` to `.\packages` relative to itself, so
every worktree pays the whole cost again. Nothing under those paths is tracked:

```powershell
Get-ChildItem . -Recurse -Directory -Filter obj | Remove-Item -Recurse -Force
Remove-Item .\bin, .\packages, .\src\cascadia\CascadiaPackage\AppPackages -Recurse -Force
```

See also "Builds that die on memory, not on code" above, which is the other cost
of building wide on this machine.
