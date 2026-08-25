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

This is not bureaucracy — it is the cheapest way to avoid building something that already exists,
was already refused, or is about to conflict with work in flight. **It has already cost us once
this session:** #20400 bumps the bundled nuget to 7.6.0, and upstream landed exactly that bump
themselves in `ede38e0a7` ("chore: update bundled nuget to 7.6", #20511) — byte-identical binary —
while our PR sat in review. Half of that PR was redundant before it was ever looked at, and a
search would have caught it.

Also check `git log upstream/main` for the area you're about to touch: upstream may have reworked
it since our last merge (per-window settings, #20328, is a live example in the settings editor).

## HARD REQUIREMENT: pay it back — open an upstream PR

**Landing a change in my fork is not the end of it.** When a change fixes a real bug or adds
something upstream would plausibly want, open a PR against microsoft/terminal as well. I benefit
from this project; contributions go back.

- The upstream PR branch is cut from **`upstream/main`** (see below), carrying only that change —
  never my fork-only commits.
- Reference the issue it closes, and follow the repo's `CONTRIBUTING.md`.
- Expect a slow queue and design pushback; that's normal here and is not a reason to skip it.
- Fork-only work — things upstream would clearly never take, or that are purely my preference —
  doesn't need a PR. Use judgement, and say which call you made.

## HARD REQUIREMENT: no Claude attribution, anywhere

**Nothing produced here credits Claude, Claude Code, or any AI — in any artifact, ever.** Not in
commit messages or trailers, not in PR titles or bodies, not in issue or review comments, not in
code comments, not in documentation, not in branch names.

I take ownership of everything that goes out under my name. DHowett stated on #20390 that he will
not merge PRs crediting Claude as a co-author, and both a `Co-Authored-By: Claude` trailer and a
"Generated with Claude Code" footer had to be scrubbed from #20400 after the fact — don't recreate
that cleanup. My global settings suppress it (`attribution: { commit: "", pr: "" }` in
`~/.claude/settings.json`); never reintroduce it by hand, and never work around it.

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
- Independence is not isolation: changes that fix a real issue still go upstream as a PR as well —
  see "Pay it back" below.

### Remotes

```
origin    https://github.com/steven-pribilinskiy/terminal.git   (fetch + push)
upstream  https://github.com/microsoft/terminal.git             (fetch only)
```

`upstream`'s push URL is deliberately set to `DISABLED_no_pushes_to_microsoft` so a stray
`git push upstream` fails loudly instead of attempting to write to Microsoft's repo.

### Open upstream PRs — branch them off `upstream/main`, never off `main`

I still have live PRs against microsoft/terminal:

- **#20390** — separate light & dark colour schemes in the Settings UI (`feature/16984-colorscheme-pair-ui`)
- **#20400** — bundled nuget 7.6.0 + refreshed CLI build guidance (`dev/steven/update-cli-build-guidance`)

**Any branch intended for an upstream PR must be based on `upstream/main`.** Branching one off my
`main` would drag every fork-only commit — *including this CLAUDE.md* — into the PR diff. Check
before pushing a PR branch: `git log --oneline upstream/main..HEAD` should show only the commits
that belong in that PR.

The no-Claude-attribution rule above applies to these branches too — and to the fork's own history.

## Build

**Prerequisites are strict and the failure messages don't name them.** VS 2026 ≥ 18.6 (v145
toolset) and the Windows 11 SDK 10.0.26100 — VS 2022 cannot build `main`. Set a machine up with the
repo's own config rather than by hand:

```powershell
winget configure .config\configuration.winget    # VS 2026 Community + Developer Mode + PowerShell 7
```

Build (x64 Release):

```powershell
Import-Module .\tools\OpenConsole.psm1
Set-MsbuildDevEnvironment
msbuild.exe .\OpenConsole.slnx /p:Configuration=Release /p:Platform=x64 /m /v:m
```

**Restore is TWO commands, not one.** `Invoke-OpenConsoleBuild` runs both, and skipping the second
leaves every project failing with *"This project references NuGet package(s) that are missing on
this computer"* — hundreds of `error :` lines that look like a broken merge and are nothing of the
sort:

```powershell
.\dep\nuget\nuget.exe restore .\OpenConsole.slnx
.\dep\nuget\nuget.exe restore .\dep\nuget\packages.config   # WIL, TAEF, CppWinRT live here
```

`NuGet.Config` pins `globalPackagesFolder` and `repositoryPath` to `.\packages`, relative to the
config file, so **every worktree needs its own full restore** — nothing is shared with the main
checkout.

**Keep the build tree on a SHORT path.** Some package references expand through unnormalised
`build\native\..\..\runtimes\...` segments, which pushes them past `MAX_PATH` (260). The linker then
reports `LNK1104: cannot open file ...TerminalThemeHelpers.lib` for a file that is present and
readable — it means "path too long", not "missing". A worktree under
`%TEMP%\claude\<session>\scratchpad\` produced a 267-character reference and failed; the same
worktree at `C:\wt-tm` produced 155 and linked. MSBuild also warns `MSB8029` about output
directories under Temp. Put worktrees at a short root.

Two more traps, both hit for real:

- **`Invoke-OpenConsoleBuild -p:Configuration=Release` silently loses the switches.** PowerShell
  parses `-p:Configuration=Release` as the `-p:` parameter with value `Configuration=Release` and
  strips the prefix, so MSBuild receives a bare `Configuration=Release` and dies with
  `MSB1008: Only one project can be specified`. Call `msbuild.exe` directly with `/p:` switches, or
  pass `--%` to stop PowerShell parsing.
- **Restore fails on `main` until #20400 lands.** The bundled `dep/nuget/nuget.exe` is 4.1.0 (2017),
  which predates `.slnx`: `Invalid input 'OpenConsole.slnx'. The file type was not recognized.`
  Restore with a current nuget instead — `nuget restore OpenConsole.slnx` from
  <https://dist.nuget.org/win-x86-commandline/latest/nuget.exe>.

## The two slots: `wtt` is yours to break, `wtd` is production

I run two locally-built Terminals side by side. **They are not two equivalent scratch installs.**

| Slot | Alias | Payload | What it is |
|---|---|---|---|
| **Test** | `wtt.exe` | `C:\TerminalSlots\test` | **The only slot you build into, register, launch, restart or replace.** Disposable. Do it as often as you like, no confirmation needed. |
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

## Deploy the test build

`tools\Deploy-TerminalSlots.ps1` is the **only** sanctioned deploy path. It already encodes the
doctrine above in its own header, and it is careful in ways a hand-rolled `makeappx`/`Add-AppxPackage`
pair is not:

```powershell
pwsh -File .\tools\Deploy-TerminalSlots.ps1   # -NoBuild skips the compile, -StageOnly skips registering
```

**`pwsh`, not `powershell`.** `OpenConsole.psm1` is `#requires -Version 7.0`, so under 5.1 the deploy
dies importing it — before building, packaging or registering anything. From WSL that is the default
you get, and piping the run through `tail` hides it further: the pipeline reports `tail`'s exit code,
so a deploy that never started reads as success.

What it does, in order: build the solution once → package the Test branding → package the Dev
branding → unpack both into `C:\TerminalSlots\{test,dev}` → write `C:\TerminalSlots\dev-pending.json`
→ **register only Test**, preserving its `settings.json` across the re-registration and asserting the
resulting `InstallLocation`.

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

### KNOWN BROKEN: `wtt` silently runs the *dev* binaries

As of 2026-08-16, launching `wtt.exe` does **not** run the Test payload. It starts, hands its command
line to the running `wtd` instance, and terminates — a new window appears in the **Dev** process,
running Dev binaries. Nothing warns you; it looks like a successful launch. Until this is fixed,
"verify in `wtt`" cannot actually be honoured while a `wtd` is running, and the honest report is
**"built and staged into the test slot, not verified"**.

`WindowEmperor::HandleCommandlineArgs` (`src/cascadia/WindowsTerminal/WindowEmperor.cpp:601`) builds
the single-instance identity — window class name *and* mutex, the same string for both (`:667`) —
from the compile-time branding token, and `build/rules/Branding.targets:7` maps every *unrecognised*
branding to `WT_BRANDING_DEV`. That catch-all tests the resulting token for emptiness, not the
branding name, so `Test` lands there with everything else. Test binaries therefore call themselves
`"Windows Terminal Dev"`, exactly like the Dev slot; `acquireMutexOrAttemptHandoff` (`:151`) finds
the Dev window, `SendMessageTimeoutW(WM_COPYDATA)` (`:187`), then `TerminateProcess` (`:672`). The
exe-path hash that would disambiguate two installs is appended only `if (!IsPackaged())` (`:631`) —
both slots are packaged, so it never applies. (The user-SID hash at `:644` *is* unconditional, but it
is identical for both slots and so discriminates nothing here.)

**The fix is not a new branding token.** `src/cascadia/TerminalApp/BuildInfo.h:1-10` already states
the intended design — the slot is resolved *at runtime* from package identity, precisely so the two
slots can stay byte-identical and a build verified in Test can be promoted into Dev unchanged. The
single-instance identity should be derived the same way: when packaged, mix the package family name
into `windowClassName`, confined to the Dev/Test catch-all arm so Release/Preview/Canary are
untouched.

**Byte-identity is already silently broken**, which must be repaired at the same time or the premise
stays a fiction. `src/common.build.post.props:77` and `src/host/proxy/Host.Proxy.vcxproj:74` both
enumerate `Canary`/`Dev`/`''` and omit `Test`, while `CascadiaPackage.wapproj` propagates
`WindowsTerminalBranding=Test` as a *global* property into every referenced C++ project. So the Test
packaging pass recompiles without `_MSVC_STL_HARDENING` and without `PROXY_CLSID_IS`, into the shared
`bin\x64\Release`. Proof on disk: `C:\TerminalSlots\dev\WindowsTerminal.exe` is 638,464 bytes,
`test\WindowsTerminal.exe` is 635,904.

Secondary defects: `Package-Test.appxmanifest` declares **no** COM CLSIDs (Dev declares 7 — monarch
proxy/stub, defterm delegation, shell extension), so no defterm handoff or Explorer verb in the Test
slot. With byte-identical binaries it cannot have distinct ones, so that is an accepted limit of the
test slot, not a bug to fix. And `WinRTUtils/inc/WtExeUtils.h:38-57` `IsDevBuild()` matches only
`WindowsTerminalDev`, so in Test `GetWtExePath()` builds a path to a `wt.exe` that does not exist.

Diagnosing this class of bug: never trust "the launch succeeded". Diff visible top-level windows per
PID across the launch and check `(Get-Process WindowsTerminal).Path` — a window that appeared under
the *Dev* executable path is the tell. `Microsoft-Windows-AppModel-Runtime/Admin` shows the container
being created and destroyed in the same second.

### Every locally-built Terminal shares one process name

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
