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

**No Claude attribution in commits or PR bodies.** DHowett stated on #20390 that he will not merge
PRs crediting Claude as a co-author. My global settings already suppress the trailer
(`attribution: { commit: "", pr: "" }`); don't reintroduce it by hand, in either repo.

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

Two traps, both hit for real:

- **`Invoke-OpenConsoleBuild -p:Configuration=Release` silently loses the switches.** PowerShell
  parses `-p:Configuration=Release` as the `-p:` parameter with value `Configuration=Release` and
  strips the prefix, so MSBuild receives a bare `Configuration=Release` and dies with
  `MSB1008: Only one project can be specified`. Call `msbuild.exe` directly with `/p:` switches, or
  pass `--%` to stop PowerShell parsing.
- **Restore fails on `main` until #20400 lands.** The bundled `dep/nuget/nuget.exe` is 4.1.0 (2017),
  which predates `.slnx`: `Invalid input 'OpenConsole.slnx'. The file type was not recognized.`
  Restore with a current nuget instead — `nuget restore OpenConsole.slnx` from
  <https://dist.nuget.org/win-x86-commandline/latest/nuget.exe>.

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
