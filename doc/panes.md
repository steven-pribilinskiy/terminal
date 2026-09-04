# Panes: titles and mouse resizing

Two things this fork adds to panes: **a title bar on each pane**, so a split tab tells you what is
in it, and **dividers you can drag with the mouse**, so a split can be adjusted without reaching
for the keyboard.

Both exist upstream only as long-running issues — pane titles as
[#4717](https://github.com/microsoft/terminal/issues/4717) (open, with
[PR #20068](https://github.com/microsoft/terminal/pull/20068) stalled in review) and mouse resize
as [#992](https://github.com/microsoft/terminal/issues/992), open since 2019 with
[PR #16895](https://github.com/microsoft/terminal/pull/16895) blocked on unanswered design
objections. See [Prior art and credits](#prior-art-and-credits) for what was taken from where.

## Pane title bars

A leaf pane can carry a one-line header above its content, showing what is running in it. Clicking
the header focuses the pane.

### `paneTitlebarVisibility`

A global setting, in the `settings.json` root or on the Settings UI's Appearance page:

```jsonc
"paneTitlebarVisibility": "multiplePanes"
```

| Value | Behaviour |
|---|---|
| `never` | No pane ever shows a header. This is how Terminal behaves without this fork. |
| `multiplePanes` | **Default.** Headers appear as soon as a tab holds more than one pane, and disappear again when it is back to one. |
| `always` | Every pane shows a header, including a tab that has never been split. |

Visibility is a property of the whole pane tree, not of one pane, so it survives splits and closes:
when closing a pane leaves a tab with a single pane again, that pane loses its header rather than
keeping a stale one. (That was the "disappearing header" defect in the upstream PR — a pane
promoted to be the tab's root had no way to be told what to do.)

The header collapses to zero height when hidden, so a tab that shows no headers renders exactly as
it did before, with no reserved space.

### What the title says

The header resolves its text in this order:

1. The pane content's own title — for a terminal pane, the title the shell sets, so `OSC 0`/`OSC 2`
   and a shell prompt that sets the window title both flow straight through.
2. The profile's name, when the content has no title yet.
3. `Terminal`, as a last resort.

A title that is an absolute path is trimmed to its last segment, so a `cd` deep into a tree does
not push the interesting part off the end of a narrow pane.

The header is exposed to assistive technology through `AutomationProperties.Name`, so a screen
reader announces which pane is focused rather than reading an unlabelled group.

### Interaction with resizing

The header's height counts as part of the pane for both minimum size and grid snapping. Without
that, `snapToGridOnResize` would be off by exactly one header on every leaf, and a pane could be
dragged smaller than its own header.

## Dragging a divider

Every split has a divider between its two children, and it can be dragged.

- **Hover** a divider and the cursor becomes a resize cursor — east-west for a vertical split,
  north-south for a horizontal one — and the divider takes on the focus border colour, so it is
  visible before you press anything. The cursor is restored when the pointer leaves.
- **Press and drag** to move the split. The pane sizes follow the pointer live.
- **Release** to commit.
- **Escape** during a drag cancels it and puts the split back where it was.
- Dragging honours the same minimum pane size the keyboard path does, so a divider stops rather
  than letting a pane collapse into nothing.

With `snapToGridOnResize` on (the default) the split still lands on whole character cells, exactly
as `alt+shift+arrow` does — the mouse path shares the existing snapping code rather than
reimplementing it. A small indicator shows the size the drag would settle on.

In High Contrast themes the divider uses the system colours instead of the theme's border brushes.

### The keyboard path is unchanged

`alt+shift+left`/`right`/`up`/`down` (`resizePane`) still work and still snap the same way. The
mouse path is an addition, not a replacement, and neither one knows about the other beyond sharing
the clamping and snapping helpers.

### Why the divider is its own element

A pane's content is a `SwapChainPanel`, which does not participate in XAML hit-testing the way an
ordinary element does. A drag implemented by watching pointer events on the panel and walking up
the pane tree to guess which divider was meant is the approach that stalled upstream. Instead each
parent pane owns four small overlay elements in its own grid — a wide invisible pointer target, a
thin visible line, a `Thumb` for keyboard and UI Automation, and the snap indicator — positioned
from the split position and refreshed whenever the row/column definitions or the pane's size
change. Pointer handlers are registered on the pane's root with `handledEventsToo`, and hit-testing
happens in that root's coordinate space.

That means the code always knows *which* divider is being dragged, because the element that
received the pointer belongs to exactly one parent pane. No tree walk, and a held reference to the
divider for the duration of the drag.

## Prior art and credits

- **[HelloThisWorld/winTerm](https://github.com/HelloThisWorld/winTerm)** (MIT) ships both features
  and is where the divider design here comes from: an explicit divider element per parent pane,
  cursor and hover feedback, and resize routed through the pane model with minimum-size clamping.
  It is a source copy of `release-1.25`, not a GitHub fork, so there is no upstream PR to point at;
  the commit worth reading is `e53ef0694`, "1.1.0 native UI and pane resize snapping". Its colours
  are its own brand palette and are **not** used here — this fork takes the divider's structure and
  applies the Terminal theme's focused/unfocused border brushes instead. Its earlier drag-to-dock
  subsystem, which that same commit deleted, is deliberately not carried over.
- **[#4717](https://github.com/microsoft/terminal/issues/4717)** is upstream's pane-titles issue,
  and **[PR #20068](https://github.com/microsoft/terminal/pull/20068)** its stalled implementation.
  The header layout here follows that PR's shape — header in row 0 of the leaf's own root, content
  in row 1, and crucially *no* extra `Grid` wrapped around the `TermControl`, which that PR
  correctly warns breaks `SwapChainPanel` rendering. The focus bug and the disappearing-header bug
  that reviewers found in it are the two behaviours called out above.
  **[#4998](https://github.com/microsoft/terminal/issues/4998)** is where the
  `never`/`multiplePanes`/`always` naming was settled, and
  **[#7290](https://github.com/microsoft/terminal/issues/7290)** covers editable titles, which this
  does not implement.
- **[#992](https://github.com/microsoft/terminal/issues/992)** is upstream's mouse-resize issue and
  **[PR #16895](https://github.com/microsoft/terminal/pull/16895)** the attempt that stalled on
  three objections: no cursor or hover affordance, a `ManipulationDelta`-bubbling design that a
  maintainer asked be replaced with a held reference to the dragged divider, and unresolved
  cell-snapping and crash reports. The design above answers all three. The one thing taken directly
  from that PR is the generalisation of `_Resize` to take a float amount, which is what lets a drag
  express a partial step.

Upstream's undo/redo of resizes, its `PaneResizeHistory`, its `balancePanes` command and its
density switch are all out of scope here.
