// The catalogue behind index.html, features.html and feature.html.
// One source of truth: the cards, the filters and the detail pages all read this.
//
// Numbers are real. commits are the actual SHAs, and ins/del/files come from
// git's own diffstat for those commits rather than an estimate.
window.FEATURES = [
  {
    id: "control-pipe", title: "Drive it from outside the process",
    cat: "automation", catLabel: "Automation",
    standout: true,
    dateAdded: "2026-08-16",
    commits: ["7ef36403f","1c4da28dd"], files: 33, ins: 2028, del: 16,
    desc: "Every window opens a local pipe another program can use: list which tabs and panes exist, read back exactly what a specific pane printed, or type into it directly — no simulated keystrokes, no guessing which window has focus. Scoped to your own user account, with a settings switch to turn it off entirely. Microsoft considered this exact capability twice (issue #9368, PR #20106) and passed; their own automation surface can't reach a pane running inside WSL.",
  },
  {
    id: "slot-system", title: "Slot badge, build info & update banner",
    cat: "deploy", catLabel: "Deploy system",
    dateAdded: "2026-08-15",
    commits: ["1f779cbc9","a766ea36b","da724f9f0","e2463ad5b","a31749e61"], files: 32, ins: 696, del: 69,
    desc: "A tab-row badge names which slot (Test/Dev) you're running, click it to copy build info, the About dialog shows commit and build time, and an update banner names what's available and where it came from.",
  },
  {
    id: "distinct-identity", title: "The test build can't impersonate the real one",
    cat: "deploy", catLabel: "Deploy system",
    dateAdded: "2026-08-17",
    commits: ["a98921578"], files: 3, ins: 41, del: 6,
    desc: "Windows only lets one instance of a given app identity run at a time, and the disposable test build used to share an identity with the live one — opening it would silently hand off to the live window and quietly run its code instead. Each now has its own distinct identity, so the disposable build actually runs, side by side with the live one.",
  },
  {
    id: "auto-promote", title: "Narrow auto-promote exception",
    cat: "automation", catLabel: "Automation",
    dateAdded: "2026-08-31",
    commits: ["416988a08"], files: 2, ins: 143, del: 3,
    desc: "A verified, read-only check (fails closed on anything ambiguous) that allows promoting a staged build without asking, only when the production window has nothing running that would be lost.",
  },
  {
    id: "ci-poll", title: "CI-poll-cadence setting",
    cat: "deploy", catLabel: "Deploy system",
    dateAdded: "2026-08-31",
    commits: ["1692ac5a9"], files: 10, ins: 97, del: 1,
    desc: "How often the background poller checks for a new CI build is a real Settings UI control now, not a fixed constant.",
  },
  {
    id: "ci-tests", title: "CI runs tests and ships the Test payload",
    cat: "deploy", catLabel: "Deploy system",
    dateAdded: "2026-08-31",
    commits: ["97a5cc18c"], files: 1, ins: 36, del: 0,
    desc: "CI used to compile and nothing else. It now runs two unit-test suites and ships a Test-slot payload alongside Dev's, so a machine that never compiles can still verify UI behaviour.",
  },
  {
    id: "window-persist", title: "Per-window position & size",
    cat: "ui", catLabel: "UI & theming",
    dateAdded: "2026-08-23",
    commits: ["552f130c2"], files: 22, ins: 763, del: 37,
    desc: "Each window remembers where it was and reopens there, instead of every new window landing in the same default spot.",
  },
  {
    id: "plain-click", title: "Plain-click links, everywhere",
    cat: "links", catLabel: "Links",
    dateAdded: "2026-08-16",
    commits: ["92cfcb8ab","7f20f24f7","9e668b8b6"], files: 15, ins: 204, del: 14,
    desc: "Links open on a plain click — no Ctrl required, toggleable in Settings — and correctly inside mouse-mode apps like tmux, where a click would otherwise just move the cursor.",
  },
  {
    id: "slack-links", title: "Slack-style bracketed links",
    cat: "links", catLabel: "Links",
    dateAdded: "2026-08-20",
    commits: ["c2dd09a42"], files: 6, ins: 61, del: 8,
    desc: "&lt;url|label&gt; links — the format Slack and many CLIs emit — become clickable with the label shown, instead of just the raw bracketed text.",
  },
  {
    id: "wsl-links", title: "WSL file:// link resolution",
    cat: "links", catLabel: "Links",
    dateAdded: "2026-08-25",
    commits: ["d0f44aab7","c15aae37a"], files: 10, ins: 585, del: 75,
    desc: "A WSL file:// link resolves to the right distro's launchable UNC path, fragments included, instead of failing to open or opening the wrong file.",
  },
  {
    id: "link-card", title: "A link card that actually fits",
    cat: "links", catLabel: "Links",
    dateAdded: "2026-08-31",
    commits: ["db520fed5","bb45059e6","7d4de07c1","cf9ce0111","69ae29d2c"], files: 37, ins: 814, del: 288,
    desc: "The plain hover tooltip became a richer card — configurable delay, a \"how to open\" hint, room for an action button — that flips to whichever side has room and never renders past the pane's edge, on either axis.",
  },
  {
    id: "icons", title: "Profile icon caching & jump-list glyphs",
    cat: "ui", catLabel: "UI & theming",
    dateAdded: "2026-08-25",
    commits: ["b8bc5aa7e","208d67c4d","088757274","c14db2649"], files: 12, ins: 722, del: 17,
    desc: "An http(s) profile icon is cached locally so settings load never blocks on the network; glyph/emoji icons get rasterized into real jump-list icons instead of a blank tile, refreshed automatically on deploy.",
  },
  {
    id: "defaults", title: "Quieter interaction defaults",
    cat: "ui", catLabel: "UI & theming",
    dateAdded: "2026-08-17",
    commits: ["88069dd16","d9acb4bf4"], files: 3, ins: 34, del: 6,
    desc: "Text selection can search with your default engine, the mouse wheel no longer changes font size or opacity by accident, and quitting doesn't ask \"close all windows?\" when there's only one open.",
  },
  {
    id: "color-schemes", title: "Separate light & dark color schemes",
    cat: "ui", catLabel: "UI & theming",
    dateAdded: "2026-08-15",
    commits: ["68e59692a"], files: 10, ins: 396, del: 223,
    desc: "Pick one scheme for light mode and a different one for dark — the terminal actually follows the OS appearance instead of being stuck on whichever scheme you set last.",
  },
  {
    id: "session-resume", title: "Bring back what was actually running",
    cat: "automation", catLabel: "Automation",
    standout: true,
    dateAdded: "2026-09-02",
    commits: ["44ad679f3","fba93b7d9","c95c0ef27","a57e09b68","b44bdcbd8"], files: 32, ins: 2392, del: 515,
    desc: "Reopening the terminal puts back the programs the panes were running, not just the shells. A coding agent returns to the same conversation with the flags it was started with; a multiplexer reattaches to the session its server is still holding; anything else can be listed by name and re-run. Panes that resume nothing repaint their saved scrollback instead, so a crash costs you the process, not the history.",
  },
];
