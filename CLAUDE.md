# Windows Terminal — steven-pribilinskiy/terminal

This checkout is **my fork, and it evolves on its own terms.** It is not a staging area for
upstream contributions. Features land here because I want them in *my* Terminal, whether or not
microsoft/terminal would ever take them.

## ALWAYS tell me when a change requires restarting the Terminal window

**Whenever a change you make or propose needs the running Windows Terminal instance restarted to
take effect, say so explicitly, and let ME decide when to restart it.** Never restart, close, or
kill a running Terminal window to "make the change take effect" or to verify your own work.

This matters more here than in most repos, because **the app I'm developing is also the app I run
my sessions in.** A restart can kill live shells, running builds, and Claude Code sessions. Same
hard rule as Tabby and WSL in my global CLAUDE.md: build and install the change, then *tell me* a
restart is needed.

Report such work as **"built and installed, not yet verified — needs a Terminal restart"**, and
say which behaviour the restart would let us confirm. If verification genuinely requires a running
UI, use **the test build (`wtt`) in its own window** — never the dev build, and never my session's
window. See "The two slots" below; that distinction is the single most important rule in this file.

### What actually needs a restart

| Change | Restart? | Why |
|---|---|---|
| `settings.json` | **No** | `AppLogic::_RegisterSettingsChange` (`src/cascadia/TerminalApp/AppLogic.cpp:303`) watches the settings *folder* for `FileName \| LastWriteTime` and calls `ReloadSettingsThrottled()` when `settings.json` changes; debounced 100 ms (`AppLogic.cpp:137`). It watches the folder, not the file, because editors write a temp file then rename it. |
| OS light/dark theme flip | **No** | `WindowEmperor.cpp:1261` reloads settings, guarded so it only fires on a real theme change (GH#15732). |
| Atlas `.hlsl` shaders | **No**, Debug only | `ATLAS_DEBUG_SHADER_HOT_RELOAD` (`src/renderer/atlas/Backend.h:31`). |
| **Any C++ change** | **Yes** | Compiled into the binaries, loaded at process start. |
| **Any XAML change** | **Yes** | XAML compiles to XBF and links into the binaries. There is no XAML Hot Reload here — Terminal is a Win32 app hosting WinUI 2 through XAML Islands. |

## HARD REQUIREMENT: never edit a tracked file in place with `sed -i`

`.gitattributes` here is `* -text`, so git stores bytes exactly as they are and normalises
nothing. Most sources in this repo are **CRLF**. GNU tools write **LF**. So a `sed -i` — or `tr`,
`awk > file`, any POSIX in-place rewrite — silently converts the entire file, and a three-line
change becomes a diff that touches every line.

**This is measured, not theoretical: a 41-line change to `ControlCore.cpp` committed as 6,149.**
It buries the real edit, makes review impossible, and conflicts with everything upstream later
does to that file.

- **Use the Edit tool.** It preserves the file's existing endings.
- If an edit genuinely has to be scripted, round-trip through `latin1` so bytes survive:
  ```bash
  node -e "const f=process.argv[1],fs=require('fs');let s=fs.readFileSync(f,'latin1');fs.writeFileSync(f,s.replace(/\r\n/g,'\n').replace(/\n/g,'\r\n'),'latin1')" <path>
  ```
- **`grep` cannot be trusted to see CR under git-bash.** In one session `grep -c $'\r'` reported
  `0` for a CRLF file and `3086` for the same file after it had been converted to LF — wrong both
  times, in both directions. Count bytes instead, which is unambiguous:
  ```bash
  cr=$(tr -cd '\r' < "$f" | wc -c); lf=$(tr -cd '\n' < "$f" | wc -c)   # cr == lf means CRLF
  ```
- **Look at `git show --stat` after every commit.** A file-sized diff for a small edit is this bug.
  Not every file is CRLF (`TerminalPage.cpp` is LF upstream), so compare against what the file was,
  never against an assumption.

A `pre-commit` hook enforces this — it refuses a commit that flips a file's endings wholesale, and
prints the repair. It lives in the repo so it survives a reclone, but `core.hooksPath` is local
config, so **each fresh clone needs it enabled once**:

```powershell
git config core.hooksPath tools/githooks
```

`--no-verify` bypasses it, for the rare case where a flip is genuinely intended.

## HARD REQUIREMENT: read the upstream issues and PRs before implementing anything

**When I ask for a feature or a fix, search microsoft/terminal's open issues and pull requests for
it BEFORE writing any code.** Not after, not "if it looks non-trivial" — first, every time.

```bash
gh search issues --repo microsoft/terminal --state open "<keywords>" --limit 20
gh search prs    --repo microsoft/terminal --state open "<keywords>" --limit 20
gh pr list   --repo microsoft/terminal --search "<keywords>" --state all --limit 20   # incl. merged/closed
git log --oneline upstream/main -20 -- <the files you would touch>
```

Then tell me what you found before you start: whether an issue already describes it (quote the
number), whether someone has an open PR for it, whether it was tried and rejected, and how the
maintainers' comments should shape the design. If there's an existing issue, the work references
it; if there's an open PR, say so and let me decide whether to build on it, wait, or go our own way.

This is not bureaucracy — it is the cheapest way to avoid building something that already exists or
was already refused, and it still pays now that nothing goes back: what upstream has already done
arrives here on the next merge, so building it again means writing a conflict for myself. **It has
cost us once already:** work went into bumping the bundled nuget to 7.6.0 while upstream landed the
identical bump themselves (`ede38e0a7`, "chore: update bundled nuget to 7.6") — a byte-identical
binary, redundant before anyone looked at it, and one search would have caught it.

Also check `git log upstream/main` for the area you're about to touch: upstream may have reworked
it since our last merge (per-window settings, #20328, is a live example in the settings editor).

## HARD REQUIREMENT: never open a pull request against microsoft/terminal

**This fork sends nothing upstream. Do not open, reopen, or prepare a PR against
microsoft/terminal — not for a bug fix, not for something they would obviously want, not "ready if
you want it later".** Do not cut a branch off `upstream/main` for that purpose, and do not ask me
whether this one is worth sending. It never is; that is what changed.

An earlier version of this file made the opposite a hard requirement, so treat any instinct to "pay
it back" as a stale rule rather than a judgement call. Landing a change on `main` is the end of it.

This is one-directional, not a break: upstream is still **merged in** for their bug and security
fixes (see "Fork policy"), and their issues and PRs are still **read** before implementing anything
(see above) — knowing a thing was already tried, already exists, or was rejected is worth as much
when you are the only consumer. What stops is anything that pushes work outward.

## HARD REQUIREMENT: no Claude attribution, anywhere

**Nothing produced here credits Claude, Claude Code, or any AI — in any artifact, ever.** Not in
commit messages or trailers, not in PR titles or bodies, not in issue or review comments, not in
code comments, not in documentation, not in branch names.

I take ownership of everything that goes out under my name. The rule stands on that alone — it was
also what upstream required, and now that nothing goes upstream it is unchanged, because the history
of this fork is mine and is read by me. A `Co-Authored-By: Claude` trailer and a "Generated with
Claude Code" footer have both had to be scrubbed after the fact here; don't recreate that cleanup.
My global settings suppress it (`attribution: { commit: "", pr: "" }` in `~/.claude/settings.json`);
never reintroduce it by hand, and never work around it.

## Fork policy

- **`main` is mine.** My own work commits straight to `main` and pushes to `origin`. No PRs, no
  feature-branch ceremony, per my global "personal projects" rule.
- **Upstream is merged in periodically, on demand** — I want their bug and security fixes, I just
  don't want to be governed by their roadmap or review queue. Merge with
  `git fetch upstream && git merge upstream/main`. Never rebase `main` onto upstream: my commits are
  already pushed, and rebasing rewrites published history.
- Conflicts are resolved **in favour of my behaviour**, not upstream's. If upstream reworks
  something I've customised, keep mine and note it in the merge commit.
- Don't "clean up" divergence from upstream. Divergence is the point.
- Nothing goes back — see "never open a pull request" above. Upstream is a source, not a destination.

### Remotes

```
origin    https://github.com/steven-pribilinskiy/terminal.git   (fetch + push)
upstream  https://github.com/microsoft/terminal.git             (fetch only)
```

`upstream`'s push URL is deliberately set to `DISABLED_no_pushes_to_microsoft` so a stray
`git push upstream` fails loudly instead of attempting to write to Microsoft's repo.

### No open upstream PRs, and no branches cut for one

There are none, and there will be none. The two that existed (separate light & dark colour schemes
in the Settings UI; the bundled nuget bump) were closed unmerged, and their work either lives on
`main` or is abandoned deliberately.

So there is no longer any reason to branch off `upstream/main`. A branch here is for my own work in
progress and is cut from `main` like any other.

The no-Claude-attribution rule above applies to these branches too — and to the fork's own history.

## HARD REQUIREMENT: builds happen in CI, never on this machine

**Do not build this repo locally. No `msbuild`, no `Invoke-OpenConsoleBuild`, no
`Deploy-TerminalSlots.ps1` run by hand, no Visual Studio build.** Push the commit and let
`.github/workflows/build.yml` produce the payload.

The reason is disk, and it is not marginal: a warm local build tree here was **40.7 GB** —
33.9 GB of it in the eight `obj\` directories, 5.5 GB in `bin\`, the rest in `packages\` and
`AppPackages\`. Every full build re-earns that, and `NuGet.Config` pins `globalPackagesFolder` and
`repositoryPath` to `.\packages` relative to the config file, so **every worktree pays it again**;
nothing is shared with the main checkout. CI has a clean machine and a package cache keyed on the
lockfiles, and it costs this disk nothing.

If a local tree ever reappears, deleting these reclaims all of it and loses nothing tracked:

```powershell
Get-ChildItem . -Recurse -Directory -Filter obj | Remove-Item -Recurse -Force
Remove-Item .\bin, .\packages, .\src\cascadia\CascadiaPackage\AppPackages -Recurse -Force
```

### Getting a build

Push to `main`, then let `tools\Fetch-CIBuild.ps1` drop the newest successful run into the Dev
staging slot (`dev-staged-ci`) — or let the Scheduled Task do it for you (see below). Promotion into
Dev is still my gesture, unchanged.

**Two things this currently costs, both worth fixing in `build.yml` before relying on it.** CI has
no test step at all — it restores, builds, packages and uploads, and that is the whole workflow — so
with local builds off **nothing runs the unit tests anywhere**. And CI ships only the Dev payload,
so `wtt` cannot be refreshed either (see "Deploy the test build").

Until both are fixed, the honest report for a code change is *"pushed, CI green — compiles and
packages; no tests run, UI behaviour not verified"*, naming the behaviour a `wtt` run would have
confirmed. Do not describe a change as verified because CI went green: green currently means only
that it compiled.

### The build tooling stays in the tree — CI needs it

`tools\Deploy-TerminalSlots.ps1` is not local-only scaffolding: `build.yml` calls it directly
(`-StageOnly`), which is exactly why the two paths cannot drift. `OpenConsole.slnx`, the `.vcxproj`
tree, `NuGet.Config`, `dep\nuget\packages.config` and `tools\OpenConsole.psm1` are all on CI's
critical path too, and `tools\Fetch-CIBuild.ps1` / `Install-CIBuildPoller.ps1` are how a CI build
reaches this machine at all. **Deleting any of them breaks CI, saves kilobytes, and — for the files
inherited from upstream — writes a permanent merge conflict.** Leave them alone.

One trap survives the move to CI, because it is about the repo and not the machine: `dep/nuget/nuget.exe`
is 4.1.0 (2017) and predates `.slnx` (*"Invalid input 'OpenConsole.slnx'. The file type was not
recognized."*). That is why `build.yml` installs a current nuget with `NuGet/setup-nuget` instead of
using the bundled copy. The PR that would have bumped it was closed unmerged and nothing goes
upstream now, so this is the steady state, not a wait.

The local-build traps that no longer apply here — `MAX_PATH` on long worktree paths, the COMMIT
exhaustion that reports itself as `C3859: ... the paging file is too small`, and
`Invoke-OpenConsoleBuild` silently eating `-p:` switches — are recorded in
[`doc/troubleshooting.md`](doc/troubleshooting.md) rather than here, so they cost nothing to read
every session.

**Never run `git worktree prune` from WSL here, and do not believe `worktree list` when it says
`prunable`.** A worktree added on the Windows side records a Windows-style path in
`.git/worktrees/<name>/gitdir` (`C:/wt-tm/.git`), which does not resolve inside WSL — `/mnt/c/...`
does. WSL git therefore reports *"gitdir file points to non-existent location"* for a worktree that
is entirely live, and a prune deregisters it. The directory survives, so nothing looks broken until
the next time you want that worktree. Check with Windows git (`/mnt/c/Program Files/Git/cmd/git.exe
-C 'C:\<path>' status`) before acting on any prunable verdict.

### When something you built will not run

Read [`doc/troubleshooting.md`](doc/troubleshooting.md) **before** you start bisecting or reading
code. It covers the slot-specific failures and, more usefully, the tests that produce confident
wrong answers — a payload exe launched directly has no package identity and aborts with
`REGDB_E_CLASSNOTREG` however healthy the build is; an "incremental rebuild" that never relinked the
exe has tested nothing; a pipeline's `$LASTEXITCODE` belongs to the last command, not to msbuild.
Every one of those sent an investigation down the wrong path on 2026-08-27.

The headline rule from that day: **after changing a `.idl` or a `.xaml`, do not trust an incremental
build.** Stale generated interfaces across DLLs msbuild thinks are up to date abort inside
`AppHost::Initialize` before any window appears. Build the whole solution, or verify it starts.

## The two slots: `wtt` is yours to break, `wtd` is production

I run two Terminals built from this repo side by side. **They are not two equivalent scratch
installs.**

| Slot | Alias | Payload | What it is |
|---|---|---|---|
| **Test** | `wtt.exe` | `C:\TerminalSlots\test` | **The only slot you may register, launch, restart or replace.** Disposable, no confirmation needed. Whatever is there now is whatever was last deployed — CI has no path to it yet, so it does not refresh on its own. |
| **Dev** | `wtd.exe` | `C:\TerminalSlots\dev` (staged) | **Production. It hosts my live agent sessions, including Claude Code sessions.** You never build into it, never register it, never launch it, never restart it, and never write to any directory it is registered from. |

The Dev slot changes exactly one way: **I press promote**, in `wtd` itself. Promotion is my gesture.
It is never a side effect of your build, and there is no situation in which you perform it for me.

### "There's no dev build running" is NOT permission

The Dev slot is production whether or not a window is open at that instant. An empty slot is not a
free slot — it is the case where overwriting it does the *most* damage, because nothing fails, no
DLL is locked, nothing warns you, and the next `wtd.exe` I launch silently runs your unverified
code. **This has already happened once** (2026-08-16): I asked for a feature, the old version of
this file pointed verification at the dev build, no `wtd` was running, and the deploy replaced the
production payload. I only found out because a process listing showed my own live work running on
it.

If you catch yourself reasoning "nothing is running there, so it's safe" — that is precisely the
inverted conclusion this section exists to prevent.

### Never

- Unpack, copy or `robocopy` into **any** directory `WindowsTerminalDev` is registered from —
  today that is `…\src\cascadia\CascadiaPackage\AppPackages\loose`, historically the fast inner
  loop, now a live grenade.
- `Add-AppxPackage -Register` anything whose identity is `WindowsTerminalDev`.
- `Start-Process wtd.exe` / `wtd.exe <args>` — launching it is my gesture too, and with the handoff
  below a launch can land inside a process I am working in.
- Kill a `WindowsTerminal.exe` whose path is under a Dev payload.
- Run `tools\Register-DevSlot.ps1`.

### Check before every deploy

`-Register` is a silent no-op when the identity is already registered elsewhere, so *assume nothing*
about where a slot points. One command, every time:

```powershell
Get-AppxPackage -Name 'WindowsTerminal*' | Select-Object Name, InstallLocation
```

If `WindowsTerminalDev`'s `InstallLocation` is the directory you were about to write to — **stop and
tell me.** Do not "just this once" it.

The two-name form (`Get-AppxPackage Dev, Test`) reads better and **throws under Windows PowerShell
5.1** — `-Name` is `[string]` there, not `[string[]]`, so it dies with a parameter-binding error
before checking anything. A mandated check that errors is a check that gets skipped, and 5.1 is what
`powershell.exe` still resolves to. The wildcard works in both shells.

Each slot has its own package identity, so each has its own settings:
`%LOCALAPPDATA%\Packages\WindowsTerminal{Dev,Test}_8wekyb3d8bbwe\LocalState\settings.json`.

**"Run the test build" means launch `wtt.exe`. It does not mean build anything.** If I want a
rebuild first I will say so.

## How a build reaches the Dev slot

One way now, on every machine:

| Build | Stages into | Marker |
|---|---|---|
| `.github/workflows/build.yml` → `tools\Fetch-CIBuild.ps1` | `dev-staged-ci` | `dev-pending-ci.json` |

`tools\Install-CIBuildPoller.ps1` registers a Scheduled Task that keeps the newest successful build
staged, so UPDATE appears without remembering to fetch anything. It never installs — promotion is
still the gesture. **Run it on the desktop too**; it was previously a notebook-only convenience and
is now the whole delivery path.

CI runs `Deploy-TerminalSlots.ps1 -StageOnly`, so the payload is produced by the same script that
used to run here — the staging, the byte-identity between the Test and Dev packages, and the
`dev-pending` contract are all unchanged. It cancels superseded runs
(`concurrency: cancel-in-progress`), because a run for a commit you have already pushed past is
building something nobody will install.

The Terminal still reads **every** `dev-pending*.json` and offers the newest, and `SlotPromotion.h`
is still the only place that decides which wins. The local-build marker `dev-pending.json` simply
stops being written; a stale one left over from before is superseded on timestamp like any other.
The promotion offer also suppresses "same commit, both clean" rather than comparing timestamps
alone — that guard was written because a CI build of the commit you are already running is always
newer than your copy of it, and without it the button never goes away however often you press it.

### Two inherited workflows are disabled, deliberately

`Publish to WinGet` (on `release: published`, submits to `microsoft/winget-pkgs`) and `Spell
checking` (every push, against an expect-list that suits upstream's content). Both are **disabled in
repo settings rather than deleted**, so an upstream merge never conflicts over them and re-enabling
is one command. The winget one matters most: publishing a release here would otherwise open a PR
against Microsoft from a fork whose whole policy is that nothing goes upstream.

## Deploy the test build

**Open gap, know it before you plan around it: CI does not deliver a Test build.** `build.yml`
uploads only `C:\TerminalSlots\dev-staged` and `dev-pending.json` (the `dev-payload` artifact), and
`tools\Fetch-CIBuild.ps1` only ever writes `dev-staged-ci`. The Test payload that `-StageOnly`
produces on the runner is thrown away with the workspace, and a runner could not register it anyway
without Developer Mode. So with local builds off, **`wtt` cannot be refreshed at all**, and any task
whose verification needs a running UI has nowhere to run.

Until CI carries the Test payload too, verification stops at what CI itself proves: the build
compiles, the packages are produced, and the unit tests it runs pass. Say so plainly — *"pushed, CI
green, UI behaviour not verified"* — rather than implying a `wtt` run happened.

`tools\Deploy-TerminalSlots.ps1` remains the only sanctioned way to produce a payload; it is simply
CI that runs it now. Its doctrine is worth knowing because CI inherits all of it: build the solution
once → package the Test branding → package the Dev branding → unpack Test into
`C:\TerminalSlots\test` and Dev into `C:\TerminalSlots\dev-staged` → **register only Test**,
preserving its `settings.json` across the re-registration and asserting the resulting
`InstallLocation` → **start `wtt` and wait for a window** → only then write `dev-pending.json`.
Under `-StageOnly`, which is what CI uses, the last two steps are skipped — so **a CI payload has
never been started even once**, and the boot check that catches a build which packages perfectly and
then dies before painting a window (2026-08-27) is not protecting you any more.

If it ever runs here again: **`pwsh`, not `powershell`.** `OpenConsole.psm1` is
`#requires -Version 7.0`, so under 5.1 the deploy dies importing it — before building, packaging or
registering anything. From WSL that is the default you get, and piping the run through `tail` hides
it further: the pipeline reports `tail`'s exit code, so a deploy that never started reads as
success. See [`doc/troubleshooting.md`](doc/troubleshooting.md).

The Dev payload is *staged and left alone*. `dev-pending.json` (commit, branch, dirty, timestamp,
payload path) is how the running `wtd` learns a newer build is waiting, so it can offer me the
promotion when I have no sessions I mind losing.

The mechanics underneath — for understanding, **not** for you to run by hand — are `makeappx unpack`
of the built `.msix` into the slot directory followed by
`Add-AppxPackage -Path <slot>\AppxManifest.xml -Register`. Note that a rebuild alone changes nothing:
the registration points at the *unpacked copy*, so refreshing the `.msix` without re-unpacking
deploys nothing. Building the package from Visual Studio emits a loose layout directly (see
`doc/building.md`) — that path is Dev-registered on this machine and is therefore off limits.

### Promotion is my gesture

`tools\Register-DevSlot.ps1` re-registers the Dev slot from `C:\TerminalSlots\dev`. It refuses to run
while any process is live under the Dev payload — re-registering an identity from a different folder
requires removing the old registration first, and Windows will not remove a running package. It
refuses rather than terminating my windows, which is the correct instinct and the one you should
share. **You do not run it.** Either I press promote in `wtd`, or I run it myself.

### FIXED: `wtt` used to silently run the *dev* binaries

**This was broken from 2026-08-16 and is fixed. Both halves were verified again on 2026-08-28 —
don't reintroduce the workarounds it used to need.**

The bug: the single-instance identity (window class name *and* mutex) came from the compile-time
branding token, and `build/rules/Branding.targets:7` maps every *unrecognised* branding to
`WT_BRANDING_DEV`. `Test` landed in that catch-all, so Test binaries called themselves
`"Windows Terminal Dev"`; `wtt.exe` found the running `wtd` window, handed off over `WM_COPYDATA` and
terminated, and the "Test" window you got was a Dev window running Dev code.

The fix (`a98921578`) is where `BuildInfo.h` said it should be — runtime package identity, not a new
branding token. `WindowEmperor::HandleCommandlineArgs` now mixes the package family name into
`windowClassName` when packaged, confined to the unrecognised-branding arm so Release/Preview/Canary
keep their handoff across upgrades. The two slots therefore have distinct single-instance identities
while staying byte-identical.

Byte-identity, which that premise depends on, is also repaired: the Test and Dev packaging passes now
produce the same bytes. Checked 2026-08-28 — `C:\TerminalSlots\test\WindowsTerminal.exe` and the
staged Dev payload had identical SHA-256 and both were 805,376 bytes. Re-check it with the one-liner
in `Deploy-TerminalSlots.ps1`'s header whenever you touch anything branding-conditional.

**What this means in practice: `wtt` can be started and verified while a `wtd` is running.** A boot
check only has to decline when a process is already running under *its own* payload; guarding on "any
`WindowsTerminal` process" would skip verification whenever a Dev window is open — nearly always —
and quietly make the check never run. `Deploy-TerminalSlots.ps1` gets this right; keep it that way.

Two real limits remain, neither worth fixing: `Package-Test.appxmanifest` declares fewer COM
registrations than `Package-Dev.appxmanifest`, so no defterm handoff or Explorer verb in the Test
slot — with byte-identical binaries it cannot have distinct ones, so that is an accepted limit rather
than a bug. And `WinRTUtils/inc/WtExeUtils.h` `IsDevBuild()` still matches only `WindowsTerminalDev`,
so in Test `GetWtExePath()` builds a path to a `wt.exe` that does not exist.

Diagnosing this class of bug: never trust "the launch succeeded". Check `(Get-Process
WindowsTerminal).Path` — a window that appeared under the *Dev* executable path is the tell.
`Microsoft-Windows-AppModel-Runtime/Admin` shows the container being created and destroyed in the
same second.

### Every Terminal built from this repo shares one process name

**`Get-Process -Name 'WindowsTerminal'` matches the test build, the dev build, *and* the Terminal my
session is running in.** Selecting `-First 1` and sending it input is how you type into my live
session. Always identify by executable path, and only ever target the test payload:

```powershell
Get-Process -Name 'WindowsTerminal' | Where-Object {
    $_.Path -like 'C:\TerminalSlots\test\*' -and $_.MainWindowHandle -ne 0
}
```

Then record pre-existing PIDs before launching, refuse any PID that already existed, and confirm the
target window is foreground before sending a keystroke. A process whose path is under a Dev payload
or under `C:\Program Files\WindowsApps\` is off limits — read it if you must, never write to it.

## Driving the UI

Automating the Settings UI means taking my keyboard and focus — see the idle rules in my global
CLAUDE.md, and use `AgentDriver.exe` (`~/projects/windows-control/agent-driver/`) rather than
hand-rolled input. Two specifics for this repo:

- **AgentDriver cannot send `Ctrl+,`**, the default Settings hotkey — its chord parser
  (`InputDriver.cs:198`) handles named keys plus single letters/digits only, no punctuation. Bind
  `Terminal.OpenSettingsUI` to a letter chord in the dev build's `settings.json` instead
  (`ctrl+shift+o` works).
- Prefer UI Automation over pixel coordinates for finding controls; it needs **Windows PowerShell
  5.1** (`Add-Type -AssemblyName UIAutomationClient`), not pwsh 7. Conversely the `GetLastInputInfo`
  idle snippet uses `Environment.TickCount64`, which does **not** exist in .NET Framework — under
  5.1 use `unchecked((uint)Environment.TickCount - dwTime)` or the type fails to compile and every
  idle check silently throws.
