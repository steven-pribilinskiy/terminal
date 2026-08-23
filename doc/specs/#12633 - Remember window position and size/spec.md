---
author: Steven Pribilinskiy <steven.pribilinskiy@gmail.com>
created on: 2026-08-19
last updated: 2026-08-19
issue id: #12633
---

# Remember window position and size

## Abstract

Terminal does not reopen a window where the user left it. The only way to get that today is to set
`firstWindowPreference` to `persistedLayout` or `persistedLayoutAndContent`, which also brings back
every tab from the previous session. Users who want the ordinary desktop behaviour - close a window,
open it again, it comes back in the same place - have no way to ask for just that.

This spec proposes a `rememberWindowGeometry` setting that saves each window's position, size and
maximized state when it closes and applies them to the next window opened under the same name,
without restoring any tabs.

## Background and prior discussion

#12633 has been open since March 2022. The discussion settled on three points that shape this design:

- The team could not agree whether this belonged under `firstWindowPreference` or under
  `launchPosition`, and asked for a short spec before an implementation
  ([comment](https://github.com/microsoft/terminal/issues/12633#issuecomment-1067338065)).
- @DHowett described the desired behaviour as "remember where i last closed a window, open the next
  window there-ish, with no session machinery at all", explicitly distinguishing it from session
  restoration.
- @lhecker recommended implementing it with `GetWindowPlacement` / `SetWindowPlacement`.

`TerminalWindow.cpp` already carries a marker for the work:

```cpp
// TODO: GH#12633: Right now, we're manually making sure that we have at least
// one tab to restore. If we ever want to come back and make it so that you can
// persist position and size, but not the tabs themselves, we can revisit this.
```

PR #20283 attempted a `restoreWindowPosition` boolean by relaxing that gate. It was closed as stale
without design review. This proposal deliberately does not relax the gate; see "Why not reuse
WindowLayout" below.

## Solution design

### The setting

A new window setting, alongside `initialPosition`, `centerOnLaunch` and `launchMode`:

```json
"rememberWindowGeometry": false
```

It is a window setting rather than a global-only one so that it participates in per-window settings
(#20328), and so it sits with the other launch-placement settings in `MTSM_WINDOW_SETTINGS`.

It is deliberately **not** a fourth `firstWindowPreference` value. `firstWindowPreference` selects
what the *first* window contains; this setting is about where *every* window is placed, is orthogonal
to tab restoration, and needs to work while `firstWindowPreference` stays at `defaultProfile` - which
is the entire point of the request.

In the Settings UI it appears as "Remember position and size", a toggle in the existing Launch
Position expander, and it is reflected in that expander's summary line so the summary does not go on
describing a launch position that is no longer in effect.

### What is stored, and where

A new `state.json` field, keyed by window name:

```json
"persistedWindowGeometries": {
    "": { "left": 320, "top": 128, "width": 1284, "height": 796, "dpi": 96, "launchMode": "default" },
    "scratch": { "left": 2200, "top": 64, "width": 960, "height": 1040, "dpi": 144, "launchMode": "maximized" }
}
```

The empty string is the key for unnamed windows, so the last unnamed window to close wins. Named
windows each keep their own entry, which means a user with a named window per task gets each one back
in its own place. The field is `FileSource::Local`, matching `persistedWindowLayouts`, so elevated
and unelevated instances do not overwrite each other.

`WindowGeometry` is a new runtimeclass rather than a reuse of `WindowLayout`:

| | `WindowLayout::InitialSize` | `WindowGeometry` |
|---|---|---|
| units | XAML DIPs | physical pixels |
| covers | `_tabContent` only | the whole window rect |
| tab row | re-added on load via hardcoded `40` / `32 + 10` constants | included |
| DPI | implicit, re-derived from the target monitor | stored explicitly |

Round-tripping a window rect through a content size and back through those hardcoded constants does
not return the original rect, so a window saved and restored repeatedly drifts by a few pixels each
cycle. Storing the real rect plus the DPI it was captured at avoids that, and makes the
monitor-changed case computable rather than guessed.

### Capture

`AppHost::PersistWindowGeometry` reads `GetWindowPlacement` and stores `rcNormalPosition`, converted
from workspace coordinates to screen coordinates by the work-area origin of the window's monitor.

Using the placement rather than `GetWindowRect` is what makes maximize behave. A maximized window's
window rect *is* its maximized rect; storing that and reopening from it produces a window that is
maximized-shaped but not maximized, and un-maximizing it does nothing useful. `rcNormalPosition` is
the rect the window would restore to, which is exactly what should be remembered. The maximized,
fullscreen and focus states travel separately in the existing `LaunchMode` flags.

Geometry is captured:

- when a window closes, in the `WM_CLOSE_TERMINAL_WINDOW` handler;
- at session end, from `_finalizeSessionPersistence` and the `WM_ENDSESSION` path;
- on the existing five-minute `_persistState` timer, so a crash or a forced shutdown does not lose it.

The timer currently runs only when `firstWindowPreference != defaultProfile`; it is widened to also
run when geometry is being remembered.

### Restore

`AppHost::_initialResizeAndRepositionWindow` gains an early branch: if there is remembered geometry
for this window's name, it is a complete answer for both position and size, so the usual
`GetLaunchDimensions` + `GetTotalNonClientExclusiveSize` computation is skipped entirely.

Precedence, highest first:

1. `contentBounds` - a window created by tearing out a tab is placed by the drag.
2. Command line: `--pos`, `--size`, `--maximized`, `--fullscreen`, `--focus`.
3. A persisted layout or workspace being restored that already carries its own geometry. The two
   mechanisms never both apply to the same window.
4. Remembered geometry.
5. `initialPosition`, `centerOnLaunch`, `launchMode`.
6. `CW_USEDEFAULT`.

The quake window is excluded; its position is derived from the monitor.

### Monitors and DPI

This is the part that decides whether the feature is pleasant or infuriating, because monitors get
unplugged, docks change resolution, and remote sessions resize.

The restore path:

1. picks the monitor with `MonitorFromRect(..., MONITOR_DEFAULTTONEAREST)`, so a monitor that no
   longer exists resolves to the nearest surviving one;
2. rescales width and height by `targetDpi / storedDpi`, so a window keeps its logical size when it
   lands on a differently-scaled display;
3. clamps the size to the target monitor's work area, so a window saved on a 4K display does not
   reopen larger than a 1080p laptop panel (#11639);
4. clamps all four edges into the work area, preferring the top-left to stay on screen, so the
   titlebar is always reachable (#3187).

Step 4 reuses the logic already in `IslandWindow::_RestoreFullscreenPosition`, which does exactly
this when leaving fullscreen, including the GH#10199 adjustment that lets the invisible resize
borders hang off the monitor. That is factored out into a shared helper rather than reimplemented.

The existing off-screen check in `_initialResizeAndRepositionWindow` - a one-pixel hit test on the
titlebar's top-left corner, which repositions but never resizes - is left exactly as it is, so
nothing changes for launches that do not use remembered geometry.

## UI/UX design

Settings UI, under Startup → Launch Position:

> **Remember position and size** &nbsp;&nbsp; `[ toggle ]`
>
> When enabled, each window's position, size and maximized state are saved when it closes, and
> restored the next time a window opens. Windows that have been given a name each remember their own.
> This takes precedence over the launch position, launch mode and "center on launch" settings above,
> but command line arguments still override it.

The overlap with `initialPosition` and `centerOnLaunch` is stated in the tooltip and in the
expander's summary line rather than by disabling those controls. Disabling them would make the
settings page appear to change on its own, which the discussion in #12633 specifically flagged as
undesirable; and the older settings still apply on the first launch, before anything has been
remembered.

### Accessibility

The toggle is a standard `ToggleSwitch` with a label and tooltip, reachable and operable by keyboard
and described by the screen reader like every other setting in the expander.

### Reliability

The remembered rect is always clamped into a real monitor's work area before use, so no stored value
- however stale, and whatever the monitor topology has done since - can produce a window the user
cannot see or reach. A malformed or zero-sized entry is ignored and the normal launch path runs.

### Compatibility

The setting defaults to `false`, so no existing installation changes behaviour. `state.json` gains a
key that older builds ignore, and an older build writing `state.json` simply drops it. Nothing about
`firstWindowPreference`, `persistedWindowLayouts` or workspaces changes.

### Performance, power and efficiency

One `GetWindowPlacement` and one small map write per window close, plus a `state.json` write that is
already debounced by one second. On launch, one map lookup and a few rect calculations before a
`SetWindowPos` that was happening anyway.

## Potential issues

- **Two windows opening at once.** Every unnamed window shares one entry, so opening several unnamed
  windows in a row stacks them. This matches the request ("open the next window there-ish") and is
  what other applications do; naming windows is the escape hatch for anyone who wants per-window
  placement.
- **A window closed while minimized.** `rcNormalPosition` is still the restored rect, so this is
  handled; the minimized state itself is deliberately not remembered, because reopening minimized
  would look like the application failed to start.

## Future considerations

- Remembering per-monitor geometry, so returning to a docking station restores the arrangement that
  was in use there. The stored record would need a monitor identifier; the current shape can be
  extended without a migration.
- Extending the clamp helper to the existing non-remembered launch path, which today can still place
  an oversized window on a small monitor.

## Resources

- [GetWindowPlacement](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowplacement)
- #3187 - Terminal startup location partially off desktop
- #11639 - failure to restore position on a secondary monitor when maximized
- #20328 - per-window settings
