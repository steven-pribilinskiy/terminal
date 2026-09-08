---
name: terminal-fork-diagnostics
description: Diagnose crashes and silent UI failures in this Windows Terminal fork (WinUI 2 / XAML Islands, C++). Use when a build fail-fasts with 0xc000027b or 0xc0000409, when tabs or controls vanish with no crash, when a re-templated control does not render, when a settings page comes up blank, or before theorising about any XAML lifetime, resource-lookup or automation problem. Covers capturing a symbol-resolved first-chance stack with cdb, verifying UI with UI Automation instead of screenshots, and the MUX/UWP traps this repo has already paid for.
---

# Diagnosing this fork

Builds happen in CI and cost ~40 minutes each, so a wrong theory is expensive
and a captured fact is cheap. This skill exists because on 2026-09-08 the same
bug was "fixed" four times by reasoning about XAML from the outside; every cycle
that captured something took one round, and every cycle that guessed took one
and taught nothing.

## Rule zero: capture, don't reason

**A XAML failure in this app almost never fails where you can see it.** The
throw is caught, or it lands a tick later during render, or it is swallowed by a
guard that exists for good reasons. Symptoms lie:

| What you see | What actually happened |
|---|---|
| Fail-fast `0xc000027b`, no frame of ours on the stack | Something threw during **measure/render** on a later tick. `CCoreServices::NWDrawTree` → `CLayoutManager::UpdateLayout`. |
| Controls silently disappear, no crash | An exception **was** thrown and **was** caught — by `_ApplyTabPosition`'s guard, a `CATCH_LOG`, or C++/WinRT's boundary. |
| "It renders nothing" from a screenshot | `PrintWindow` cannot capture DirectComposition content in XAML Islands. It is a capture artifact, not a rendering failure. |

So: get the first-chance stack before forming a theory. `scripts/Capture-FirstChance.ps1`
does the whole thing.

```powershell
# Stage the PDBs for the build under test first - without them every frame of
# ours decodes to TerminalApp!DllGetActivationFactory+<offset> and names nothing.
.\tools\Fetch-CIBuild.ps1 -RunId <id> -WithSymbols
.\tools\Refresh-TestSlot.ps1 -NoLaunch

.\.claude\skills\terminal-fork-diagnostics\scripts\Capture-FirstChance.ps1 `
    -Setting tabPosition -From top -To left
```

It launches `wtt` in the `-From` state, attaches cdb, flips the setting in
`settings.json` (which hot-reloads: `AppLogic::_RegisterSettingsChange` watches
the folder), and logs every first-chance throw with a stack.

### Reading the log

The gold is not the stack, it is the WIL line just after it:

```
D:\a\terminal\terminal\src\cascadia\TerminalApp\TerminalPage.cpp(715)\TerminalApp.dll!...:
  LogHr(2) tid(107e8) 802B000A
  Msg:[winrt::hresult_error: Cannot find a Resource with the Name/Key TabViewButtonStyle]
```

That is **file, line, HRESULT and message** — the entire diagnosis in one line.
Grep for `LogHr|ReturnHr|Msg:\[` first and only read stacks if those are absent.

| HRESULT | In this codebase it has meant |
|---|---|
| `0x802B000A` | A `{ThemeResource}`/`{StaticResource}` key a template cannot see. |
| `0x8000FFFF` | Adding a XAML element that **already has a parent**. |
| `0x80004005` | Generic `hresult_error` re-thrown at a WIL boundary; look further in. |
| `0xc0000409` | `__fastfail` - a stowed exception that nothing caught. |

### Make the next failure measure itself

When a fix might not work, ship the fix **and** a log line that states the
outcome in numbers. `restored 0 of 1 tabs` turned a third guess into a
one-cycle answer. A silent failure is the bug's best defence; take it away.

## Verify with UI Automation, not screenshots

`scripts/Get-UiaSnapshot.ps1` reports control types, names and bounding
rectangles for the Test slot's window. Geometry is proof; a screenshot is not:

```powershell
# Requires Windows PowerShell 5.1 - UIAutomationClient is not in pwsh 7.
powershell.exe -NoProfile -File .\.claude\skills\terminal-fork-diagnostics\scripts\Get-UiaSnapshot.ps1
```

Counting `TabItem` before and after an action is how the vanishing-tabs bug was
pinned down, and how each attempted fix was scored (`1,0,0,0,0`).

**Two traps in the tooling itself.** UIA `Invoke()` on a `SettingsCard` silently
does nothing - `SettingsCardAutomationPeer` advertises the Invoke pattern but
`ButtonBaseAutomationPeer` supplies no `IInvokeProvider`, so an automated click
on a settings card proves nothing either way. And `FindFirst` by name alone will
happily return a `Text` element that shares the name with the `Button` you
wanted; always pair the name with a `ControlType`.

To open the Settings UI without touching the keyboard, post the system-menu
command (`WM_SYSCOMMAND`, id `4096`) rather than synthesising `Ctrl+,`.

## Traps this repo has already paid for

**MUX's Generic.xaml helper styles are `x:Name`, not `x:Key`.**
`TabViewButtonStyle` and `TabViewScrollViewerStyle` resolve inside MUX's own
dictionary and nowhere else. A copy of a MUX template living in an app
dictionary cannot reference them - the lookup throws `0x802B000A` part-way
through expanding the template. Copy the *markup* you need, never the key.

**A half-expanded ControlTemplate kills the window one tick later.** If template
expansion throws, the control keeps the broken visual tree; XAML fails on the
next measure with no catchable frame. Whenever a template is applied inside a
`try`, the `catch` must `ClearValue(FrameworkElement::StyleProperty())` so a bad
template degrades instead of fail-fasting.

**An explicit `Style` does NOT remove a control's default template.** UWP applies
the built-in style from `DefaultStyleKey` underneath `FrameworkElement::Style`,
so a `Style` with no `Template` setter layers onto it. Proof in this repo:
`App.xaml`'s implicit `primitives:TabViewListView` style sets only
`ItemContainerTransitions`, and `ColorButtonStyle` is applied by key to the
colour-picker buttons - neither sets a `Template` and both render. Do not "fix"
a missing-template theory without checking this first.

**`TabView.TabItems` holds live `TabViewItem` elements, not data.**
`TabManagement.cpp` inserts them directly. Consequences:
- Re-templating the `TabView` builds a new list, and elements that still have a
  parent never arrive in it - the strip empties and stays empty.
- Neither `Clear()` nor a following `UpdateLayout()` unparents them. The panel
  still holding them belongs to a template that no longer exists, so nothing
  will ever release it on its own; remove the child from its parent `Panel`'s
  `Children` explicitly.
- Prefer not re-templating at all. `ItemsControl::ItemsPanelRoot()` exposes the
  realized panel, so `ItemsStackPanel::Orientation` can be flipped in place, and
  a `Grid`'s row/column definitions can be rebuilt at runtime - neither
  re-parents anything.

**`GetTotalNonClientExclusiveSize().height` includes the titlebar** for
`NonClientIslandWindow`. Using it as a window height overhangs the work area by
a whole titlebar, which is how a docked window ended up on top of the taskbar.
For anything that should sit inside the work area, add only the invisible resize
border - the same thickness on every side, already halved elsewhere to compute
`left`.

**`Grid::SetRow`/`SetColumn`/`SetRowSpan`/`SetColumnSpan` take a
`FrameworkElement`,** while `Grid::Children().Append` takes a `UIElement`. A
`try_as<UIElement>` on something you are about to position is a compile error
one CI round away.

## Gates that cost seconds and save a round trip

Run these before pushing anything:

```powershell
.\tools\Check-SettingsModelConsistency.ps1   # missing resw strings, enum labels
```

That one matters most: a missing resource string compiles, packages, passes both
test suites, and then access-violates the first time the Settings UI asks for a
name. Also worth the few seconds - parse what you edited:

```powershell
Get-Content .\src\cascadia\TerminalSettingsModel\defaults.json -Raw | ConvertFrom-Json
[xml](Get-Content .\src\cascadia\TerminalSettingsEditor\Resources\en-US\Resources.resw -Raw)
py -3 -c "import yaml; yaml.safe_load(open('.github/workflows/build.yml',encoding='utf-8'))"
```

And check line endings survived every edit, because most sources here are CRLF
and `.gitattributes` normalises nothing:

```bash
cr=$(tr -cd '\r' < "$f" | wc -c); lf=$(tr -cd '\n' < "$f" | wc -c)   # cr==lf means CRLF
```

## Slot rules that apply while debugging

`wtt` is the only slot to launch, attach to, or kill. Identify by executable
path (`C:\TerminalSlots\test\...`) - every Terminal built from this repo shares
the process name, including the one the session is running in. See the repo
`CLAUDE.md`; nothing here overrides it.

Killing cdb while it is attached takes the debuggee with it. Use `.detach` if
the window needs to survive the capture.
