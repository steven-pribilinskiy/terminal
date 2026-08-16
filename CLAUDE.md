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
UI, use the dev build in its own window (see Deploy below) rather than my session's window.

### What actually needs a restart

| Change | Restart? | Why |
|---|---|---|
| `settings.json` | **No** | `AppLogic::_RegisterSettingsChange` (`src/cascadia/TerminalApp/AppLogic.cpp:303`) watches the settings *folder* for `FileName \| LastWriteTime` and calls `ReloadSettingsThrottled()` when `settings.json` changes; debounced 100 ms (`AppLogic.cpp:137`). It watches the folder, not the file, because editors write a temp file then rename it. |
| OS light/dark theme flip | **No** | `WindowEmperor.cpp:1261` reloads settings, guarded so it only fires on a real theme change (GH#15732). |
| Atlas `.hlsl` shaders | **No**, Debug only | `ATLAS_DEBUG_SHADER_HOT_RELOAD` (`src/renderer/atlas/Backend.h:31`). |
| **Any C++ change** | **Yes** | Compiled into the binaries, loaded at process start. |
| **Any XAML change** | **Yes** | XAML compiles to XBF and links into the binaries. There is no XAML Hot Reload here — Terminal is a Win32 app hosting WinUI 2 through XAML Islands. |

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

## The two slots: `wtd` (dev) and `wtt` (test)

I run two locally-built Terminals side by side, and I refer to them by those names:

| I say | Means | Alias | Payload | Purpose |
|---|---|---|---|---|
| "the dev build" | Dev slot | `wtd.exe` | `C:\TerminalSlots\dev` (staged), currently registered from `…\projects\terminal\src\cascadia\CascadiaPackage\AppPackages\loose` | where real work happens; restarting it is **my** call |
| "the test build" | Test slot | `wtt.exe` | `C:\TerminalSlots\test` | disposable; replace and restart it as often as you like |

**"Run the test build" means launch `wtt.exe`. It does not mean build anything.** If I want a
rebuild first I will say so. Same for "run the dev build" → `wtd.exe`. `tools\Deploy-TerminalSlots.ps1`
is what *produces* the slots (build → package both brandings → register Test → stage Dev); it is a
separate, explicit request.

Each slot has its own package identity, so each has its own settings:
`%LOCALAPPDATA%\Packages\WindowsTerminal{Dev,Test}_8wekyb3d8bbwe\LocalState\settings.json`.

### KNOWN BROKEN: `wtt` silently runs the *dev* binaries

As of 2026-08-16, launching `wtt.exe` does **not** run the Test payload. It starts, hands its
command line to the running `wtd` instance, and terminates — a new window appears in the **Dev**
process, running Dev binaries. Nothing warns you; it looks like a successful launch.

`WindowEmperor::HandleCommandlineArgs` (`src/cascadia/WindowsTerminal/WindowEmperor.cpp:485`)
builds the single-instance identity — window class name *and* mutex — from the compile-time
branding token, and `build/rules/Branding.targets:7` maps every unrecognised branding to
`WT_BRANDING_DEV`. So Test binaries call themselves `"Windows Terminal Dev"`, exactly like the Dev
slot; `acquireMutexOrAttemptHandoff` (:151) finds the Dev window, `SendMessageTimeoutW(WM_COPYDATA)`,
then `TerminateProcess` (:543). The path/SID hash that would disambiguate two installs is appended
only `if (!IsPackaged())` — both slots are packaged, so it never applies.

This invalidates the premise `Deploy-TerminalSlots.ps1` is built on ("Test and Dev differ only in
their manifest, so one compile serves both"). A working Test slot needs a real `WT_BRANDING_TEST`
token and its own `windowClassName`, i.e. **separately compiled binaries** — which also means what
you verify in `wtt` is no longer byte-for-byte what gets promoted to `wtd`. Secondary defect:
`Package-Test.appxmanifest` declares **no** COM CLSIDs at all (Dev declares 7 — monarch proxy/stub,
defterm delegation, shell extension), so defterm handoff would not work in the Test slot either;
those GUIDs must also be distinct from Dev's or the two slots fight over the defterm registration.

Diagnosing this class of bug: never trust "the launch succeeded". Diff visible top-level windows
per PID across the launch and check `(Get-Process WindowsTerminal).Path` — a window that appeared
under the *Dev* executable path is the tell. `Microsoft-Windows-AppModel-Runtime/Admin` shows the
container being created and destroyed in the same second.

## Deploy the dev build

The build produces an msix; registering it as a loose layout is the fast inner loop:

```powershell
$pkg   = 'src\cascadia\CascadiaPackage\AppPackages\CascadiaPackage_0.0.1.0_x64_Test'
$loose = 'src\cascadia\CascadiaPackage\AppPackages\loose'
makeappx unpack /o /p "$pkg\CascadiaPackage_0.0.1.0_x64.msix" /d $loose
Add-AppxPackage -Path "$loose\AppxManifest.xml" -Register -ForceUpdateFromAnyVersion
```

Launches as **`wtd.exe`** ("Windows Terminal Dev"). Release builds have no `_Debug` infix in the
folder or msix name; Debug builds do.

**A rebuild alone changes nothing**: the package is registered against `loose\`, which is a *copy*
made by `makeappx unpack`. Rebuilding refreshes the `.msix`, not `loose\`. The full loop is close
the dev window (it locks the DLLs) → rebuild → re-unpack → re-register → relaunch. Building the
package **from Visual Studio** emits the loose layout directly and registers it, skipping the msix
step — much faster (see `doc/building.md`).

The dev build's settings live at
`%LOCALAPPDATA%\Packages\WindowsTerminalDev_8wekyb3d8bbwe\LocalState\settings.json`, separate from
my real Terminal's.

### The dev build and my session share a process name

**`Get-Process -Name 'WindowsTerminal'` matches BOTH the dev build and the Terminal my session is
running in.** Selecting `-First 1` and sending it input is how you type into my live session.
Always identify the dev build by executable path:

```powershell
Get-Process -Name 'WindowsTerminal' | Where-Object {
    $_.Path -like '*\AppPackages\loose\*' -and $_.MainWindowHandle -ne 0
}
```

Then record pre-existing PIDs before launching, refuse any PID that already existed, and confirm
the target window is foreground before sending a keystroke.

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
