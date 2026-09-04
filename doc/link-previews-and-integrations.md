# Link previews and integration plugins

Hovering a link (or a piece of plain text that a rule recognizes) in the terminal can show a
card with live information pulled from an external tool — a GitHub pull request's state and
checks, a Jira issue's summary and status, a Slack message's author and text, a Stith session's
name and state. This is driven by **integration plugins**: small JSON manifests that describe how
to recognize a match, how to fetch data for it, and how to display the result. Four ship built in:
**GitHub**, **Jira**, **Slack**, and **Stith**.

This document covers the feature end to end: what ships out of the box, the Integrations
settings page, text-pattern matching, the plugin manifest format for anyone writing their own,
and the security rules that keep credentials from leaking.

## What a link preview is

Hover a hyperlink (or a text match — see below) that a plugin recognizes, and the hyperlink card
grows a section below the usual link target: an icon and name for the plugin, then a small set of
fields fetched from that tool. The card shows a loading state while the fetch is in flight, and
caches the result for the plugin's configured lifetime so a second hover is instant.

A card is not limited to a list of fields. A plugin can also declare:

- **[Field groups](#field-groups)** — named sets of fields ("Details", "Development") that the
  card keeps together and the settings page turns on and off as a unit.
- **[Tabs](#tabs)** — secondary content behind a small tab strip: a description rendered as
  Markdown or Atlassian Document Format, or a list of comments with author, avatar and time.
- **[Actions](#actions)** — something the card can *do* to the thing behind the link. Jira's
  transition picker is the worked example: choose a transition, press Apply, and the status badge
  changes.

The same preview can also be opened in a pane instead of a tooltip, via **Show in pane** on the
card — see [`panes.md`](panes.md) for the pane surface itself.

If the plugin isn't configured (missing a required setting or credential), the card shows a muted
"`<Plugin>`: not configured" line instead of attempting a fetch. If a fetch fails, the card shows
the error inline (for example, "`Jira: 401 Unauthorized`") — it stays usable, just without a
preview.

## Built-in plugins

| Plugin | Recognizes | Needs | Notes |
|---|---|---|---|
| **GitHub** | `https://github.com/<owner>/<repo>/pull/<n>`, `/issues/<n>` and `/commit/<sha>` links | Nothing, if the [GitHub CLI](https://cli.github.com) is signed in. Otherwise a personal access token (credential) | Tries `gh auth token` first and falls back to a stored token — see [Authenticating from a local CLI](#authenticating-from-a-local-cli-the-gh-then-token-pattern). Tabs for the description and comments; Details / Activity / Commit field groups. |
| **Jira** | `https://<host>/browse/<KEY>` links, and (opt-in) issue keys like `CAB-8209` in plain text | Site host (setting) + account email and API token (credentials) | Credentials are only ever sent to the configured host — see [Host guarding](#host-guarding). Create an API token at `id.atlassian.com` → Security → API tokens. Carries the transition [action](#actions), a Development [field group](#field-groups) fed by Jira's `dev-status` API, and Description / Comments [tabs](#tabs). |
| **Slack** | `https://<workspace>.slack.com/archives/<channel>/p<ts>` permalinks, including thread replies (`?thread_ts=`) | A bot token (credential) | The token needs the `channels:history`, `groups:history`, and `users:read` scopes, and the bot must be a member of the channel it's reading. |
| **Stith** | `stith://session/<id>`, `stith://focus/<id>`, and `https://<server>/(s\|agent\|sessions\|embed/s)/<id>` links | Server URL (setting) | No credentials. The fetch allows an untrusted certificate, so a self-signed `lvh.me` cert doesn't block the preview. A [`detectPatterns`](#detectpatterns) entry makes a bare `stith://…` in plain output hoverable without OSC 8. |

Each plugin's fields (which pieces of data show on the card, and in what order) can be trimmed on
the Integrations settings page — see below.

## The Integrations settings page

The Integrations page lists every discovered plugin (built-in and user-installed) with its
source and an enable/disable toggle. Opening a plugin shows:

- **Settings** — plain, non-secret values like a site host or server URL. These are stored in
  `settings.json` under `"integrations"` (see [Where configuration lives](#where-configuration-lives-in-settingsjson)).
- **Credentials** — a password box per credential field, with a "stored" indicator and a Clear
  button once one is saved. Saving writes straight to the Windows credential vault; it never
  touches `settings.json`.
- **Show in tooltip** — a checkbox per display field, to choose which ones appear on the card and
  in what order. Leaving all of them unchecked falls back to the manifest's own defaults. When the
  manifest declares [field groups](#field-groups), the checkboxes are grouped under a tri-state
  header checkbox that selects or clears the whole group; fields that belong to no group fall into
  an implicit "Details" group.
- **Tabs** — a checkbox per [tab](#tabs) the manifest declares. With no tab enabled the card shows
  no tab strip at all.
- **Text matching** — any of the plugin's *suggested* text matchers (see below), each with an
  **Add as rule** button that creates a matching Link Tooltip rule for you.
- **Source** — read-only, either `built-in` or the path of the manifest file it was loaded from.

### Where configuration lives (in `settings.json`)

Only non-secret settings and field selections are persisted to `settings.json`, under a top-level
`"integrations"` object keyed by plugin id:

```jsonc
"integrations": {
    "jira": {
        "enabled": true,
        "settings": { "host": "acme.atlassian.net" },
        "fields": [ "summary", "status", "assignee", "updated" ],
        "tabs": [ "description", "comments" ]
    }
}
```

- `enabled` — whether the plugin is used for matching and preview at all.
- `settings` — the plugin's non-secret setting values, keyed by the manifest's setting `key`.
- `fields` — the display field keys to show, in order. Omit it (or leave it unset) to use the
  manifest's own `"default": true` fields.
- `tabs` — the [tab](#tabs) keys to show, in order. Omit it to use the manifest's own
  `"default": true` tabs.

### Where credentials live

Credentials are **never** written to `settings.json`. They're stored in the Windows credential
vault (Credential Manager → Web Credentials), one vault "resource" per plugin, one credential per
field key:

```
Resource: WindowsTerminal/Integrations/<plugin id>
User name: <credential key>       e.g. "email", "token"
```

For example, saving Jira's email and token creates two Web Credentials entries under
`WindowsTerminal/Integrations/jira`. The vault is per Windows user, so both Terminal build slots
(and every Terminal built from this repo) see the same stored credentials — that's intended, not
a leak.

## Text matching

Link previews aren't limited to actual hyperlinks. A **Link Tooltip rule** with `"match": "text"`
turns an arbitrary ICU regular expression into a hoverable, clickable match anywhere it appears in
terminal output — not just inside a real link.

For example, Jira's built-in text matcher recognizes issue keys like `CAB-8209` in plain output.
Once a text rule for that pattern is enabled, any matching text becomes hoverable: the card shows
the same Jira preview a real `/browse/` link would, Open navigates to the issue, and Copy link
copies its URL.

Rules of the road:

- Text patterns are scanned over the visible viewport (± one page) on every output-idle rescan,
  same as the built-in URL and file-path detectors.
- Up to **16** text patterns can be active at once.
- The **first enabled rule** (in list order) whose criteria match wins — overrides from multiple
  matching rules are not merged.
- A plugin's suggested text matcher can be turned into a rule with one click, via **Add as rule**
  on the Integrations page. That's the easiest way to enable one; a rule can also be written by
  hand on the Link Tooltip page.

### Link Tooltip rule fields relevant to previews

A `HyperlinkTooltipRule` (Link Tooltip page, `settings.json` under a rules array) has these
preview-related keys:

| JSON key | Values | Meaning |
|---|---|---|
| `match` | `"link"` (default) \| `"text"` | Whether `pattern` runs against a detected/OSC 8 hyperlink's URI, or is scanned over plain terminal text. |
| `pattern` | regex string | Link rules: an ECMAScript regex searched in the link text. Text rules: an ICU regex scanned over the buffer; the whole match becomes the hoverable text. |
| `integration` | `""` (auto) \| `"none"` \| a plugin id | Which plugin previews a match. Empty lets the plugins' own matchers decide; `"none"` disables preview for links this rule matches. |
| `preview` | boolean, default `true` | Whether to show a preview at all for links/text this rule matches. |
| `buttons` | array of string | Which buttons the card shows for this rule: `open`, `copyLink`, `copyPath`, `reveal` (Show in Explorer), `showInPane`. An empty or absent list inherits the global `hyperlink.tooltipButtons`. |
| `showInPane` | boolean, unset = inherit | Open the preview in a pane instead of a tooltip for this rule. Unset inherits the global `hyperlink.previewInPane`. |

Three global settings back these, in `settings.json` alongside the rules:

| JSON key | Default | Meaning |
|---|---|---|
| `hyperlink.tooltipButtons` | `["copyLink", "showInPane"]` | The buttons every rule shows unless it overrides them. **Open is off by default** — a card is for looking, and the link is still clickable. |
| `hyperlink.tooltipHint` | `true` | Whether the card ends with the "Click to follow link" line. |
| `hyperlink.previewInPane` | `false` | Hovering opens the preview in a pane rather than a card. |

**Show in pane** puts the same preview — header, tabs, field groups, actions — into a real pane
with room to read it, and offers a "Pane only — hide tooltips" switch that silences hover cards
while the pane is open and restores them when it closes. See [`panes.md`](panes.md).

## Writing a plugin manifest

A plugin is one folder containing `integration.json`. User plugins live under:

```
%LOCALAPPDATA%\Microsoft\Windows Terminal\Integrations\<folder>\integration.json
```

(A bare `<name>.json` directly in that `Integrations` folder is also picked up.) Discovery order
is: built-ins first, then the user directory — **a user manifest with the same `id` replaces a
built-in of the same id**, so a plugin can be forked, edited, and dropped in place of a shipped
one. `IntegrationRegistry::Refresh()` re-scans on every settings reload; a manifest that fails to
parse is skipped and logged, never fatal.

### Full annotated example (Jira)

```jsonc
{
    "id": "jira",
    "name": "Jira",
    "icon": "",
    "version": 1,
    "cacheSeconds": 300,

    // Plain values the user fills in on the Integrations page. Stored in settings.json.
    "settings": [
        {
            "key": "host",
            "label": "Site host",
            "placeholder": "acme.atlassian.net",
            "description": "The Jira Cloud site (or Jira Server host) whose links to preview. Credentials are only ever sent to this host.",
            "required": true
        }
    ],

    // Secret values. Stored in the Windows credential vault, never in settings.json.
    // A credential is treated as secret unless its key is "email", "user", or "username",
    // or it explicitly sets "secret": false.
    "credentials": [
        { "key": "email", "label": "Account email", "description": "The Atlassian account the API token belongs to.", "secret": false },
        { "key": "token", "label": "API token", "description": "Create one at id.atlassian.com -> Security -> API tokens.", "secret": true }
    ],

    // How this plugin claims a link or a piece of text.
    "matchers": [
        {
            "kind": "link",
            "pattern": "^https?://[^/]+/browse/(?<key>[A-Z][A-Z0-9]+-\\d+)",
            // The hovered URI's host must equal the "host" setting's value, or this
            // matcher does not fire -- see "Host guarding" below.
            "hostSetting": "host"
        },
        {
            "kind": "text",
            "pattern": "\\b(?<key>[A-Z][A-Z0-9]{1,9}-\\d{1,7})\\b",
            // Offered on the Integrations page as "Add as rule".
            "suggested": true,
            "description": "Issue keys in output, like CAB-8209",
            // How a text match becomes a URL for Open / Copy link / click.
            "link": "https://{{settings.host}}/browse/{{key}}"
        }
    ],

    // The request(s) that fetch preview data. Steps run in order; a later step can
    // read an earlier step's result through {{stepId:/json/pointer}}.
    "fetch": [
        {
            "id": "issue",
            "type": "http",
            "method": "GET",
            "url": "https://{{settings.host}}/rest/api/3/issue/{{key}}?fields=summary,status,resolution,assignee,priority,issuetype,updated,reporter,description,comment",
            "auth": { "type": "basic", "user": "{{credentials.email}}", "password": "{{credentials.token}}" },
            "headers": { "Accept": "application/json" },
            "timeoutMs": 8000
        },
        // The options for the transitions action, below.
        { "id": "transitions", "optional": true, "url": "https://{{settings.host}}/rest/api/3/issue/{{key}}/transitions?expand=transitions.fields", /* … */ },
        // Jira's Development panel. Undocumented and not present on every site,
        // hence "optional"; both need the numeric issue id the first step returned.
        { "id": "devsummary", "optional": true, "when": "{{issue:/id}}", "url": "https://{{settings.host}}/rest/dev-status/latest/issue/summary?issueId={{issue:/id}}", /* … */ },
        { "id": "devdetail",  "optional": true, "when": "{{issue:/id}}", "url": "https://{{settings.host}}/rest/dev-status/latest/issue/detail?issueId={{issue:/id}}&applicationType=GitHub&dataType=pullrequest", /* … */ }
    ],

    // Which fields belong together, for the card and for the settings checkboxes.
    "fieldGroups": [
        { "key": "details",     "label": "Details",     "fields": [ "summary", "status", "resolution", "assignee", "priority", "type", "reporter", "updated" ] },
        { "key": "development", "label": "Development", "fields": [ "devBranches", "devCommits", "devPullRequests", "devBuilds", "devDeployments" ] }
    ],

    // The card's field list. "default": true fields show unless the user picks a
    // different set on the Integrations page. With more than one fetch step,
    // every path has to name its step: an unqualified path reads whichever step
    // ran last, which is not the one you meant.
    "fields": [
        { "key": "summary",  "label": "Summary",  "path": "issue:/fields/summary",              "kind": "title",  "default": true },
        { "key": "status",   "label": "Status",   "path": "issue:/fields/status/name",          "kind": "badge",  "colorPath": "issue:/fields/status/statusCategory/colorName", "default": true },
        { "key": "assignee", "label": "Assignee", "path": "issue:/fields/assignee/displayName", "kind": "text",   "iconPath": "issue:/fields/assignee/avatarUrls/24x24", "default": true },
        { "key": "priority", "label": "Priority", "path": "issue:/fields/priority/name",        "kind": "text",   "iconPath": "issue:/fields/priority/iconUrl", "default": false },
        { "key": "type",     "label": "Type",     "path": "issue:/fields/issuetype/name",       "kind": "text",   "iconPath": "issue:/fields/issuetype/iconUrl", "default": false },
        { "key": "reporter", "label": "Reporter", "path": "issue:/fields/reporter/displayName", "kind": "text",   "iconPath": "issue:/fields/reporter/avatarUrls/24x24", "default": false },
        { "key": "updated",  "label": "Updated",  "path": "issue:/fields/updated",              "kind": "text",   "format": "relativeTime", "default": true },

        { "key": "devBranches",     "label": "Branches",      "path": "devsummary:/summary/branch/overall/count",      "kind": "text", "default": false },
        { "key": "devCommits",      "label": "Commits",       "path": "devsummary:/summary/repository/overall/count",  "kind": "text", "default": false },
        { "key": "devPullRequests", "label": "Pull requests", "path": "devsummary:/summary/pullrequest/overall/count", "kind": "text", "default": false }
    ],

    // Secondary content behind a tab strip. See "Tabs" below.
    "tabs": [
        { "key": "description", "label": "Description", "kind": "body", "path": "issue:/fields/description", "format": "adf", "default": true },
        { "key": "comments",    "label": "Comments",    "kind": "list", "path": "issue:/fields/comment/comments", "format": "adf",
          "itemAuthorPath": "/author/displayName", "itemAvatarPath": "/author/avatarUrls/24x24",
          "itemBodyPath": "/body", "itemTimePath": "/created", "default": false }
    ],

    // Something the card can do, not just show. See "Actions" below for the
    // full version of this one.
    "actions": [
        { "key": "transition", "label": "Move to", "kind": "choice", "optionsPath": "transitions:/transitions", /* … */ }
    ]

    // "html": "…" — off by default in this build; see "HTML representation" below.
}
```

### Manifest reference

Top-level keys:

| Key | Type | Meaning |
|---|---|---|
| `id` | string, required | Unique plugin id. A missing `id` makes the whole manifest invalid. |
| `name` | string | Display name. Defaults to `id` if omitted. |
| `icon` | string | Icon shown next to the plugin's name. |
| `version` | number | Manifest schema version, informational. |
| `cacheSeconds` | number | How long a fetched preview is cached per matched text/link. Negative values are clamped to `0`. |
| `settings` | array of [field](#settings--credentials-fields) | Non-secret configuration, stored in `settings.json`. |
| `credentials` | array of [field](#settings--credentials-fields) | Secret configuration, stored in the credential vault. |
| `matchers` | array of [matcher](#matchers) | How the plugin claims a link or text match. |
| `fetch` | array of [fetch step](#fetch-steps) | The request pipeline that produces preview data. |
| `fields` | array of [display field](#display-fields) | How the fetched result renders on the card. |
| `fieldGroups` | array of [field group](#field-groups) | Groups the display fields into named sets for the card and the settings checkboxes. |
| `tabs` | array of [tab](#tabs) | Secondary content — a description, a comment list — shown behind a tab strip. |
| `actions` | array of [action](#actions) | Things the card can do to the thing behind the link. |
| `detectPatterns` | array of ICU regex | Patterns the terminal scans plain output for while this plugin is enabled, so text it owns becomes hoverable without OSC 8. See [detectPatterns](#detectpatterns). |
| `html` | string | A full HTML document string, rendered in place of `fields`. Compiled in but off by default in this build; see [HTML representation](#html-representation-preview-off-by-default). |

#### Settings / credentials fields

| Key | Type | Meaning |
|---|---|---|
| `key` | string | Referenced from templates as `{{settings.<key>}}` / `{{credentials.<key>}}`. |
| `label` | string | Shown on the Integrations page. Defaults to `key` if omitted. |
| `placeholder` | string | Placeholder text for the input box. |
| `description` | string | Help text shown under the field. |
| `required` | boolean | Settings only, in effect: a plugin isn't fetched from until every `required` setting and credential has a value. |
| `secret` | boolean | Credentials only. Defaults to `true` unless the key is `email`, `user`, or `username` — those default to visible, non-masked text. |

#### Matchers

| Key | Type | Meaning |
|---|---|---|
| `kind` | `"link"` \| `"text"` | Whether `pattern` runs against a hovered link's URI, or is scanned over terminal text. |
| `pattern` | ICU regex | Named capture groups (`(?<name>…)`) become template variables. |
| `hostSetting` | string | **Link matchers only.** The URI's host must equal the named setting's current value, or this matcher does not fire. See [Host guarding](#host-guarding). |
| `link` | template string | **Text matchers only.** How a text match turns into a URL for Open / Copy link / click. |
| `suggested` | boolean | **Text matchers only.** Offered on the Integrations page's "Add as rule" list. |
| `description` | string | Shown next to a suggested text matcher. |

#### Fetch steps

| Key | Type | Meaning |
|---|---|---|
| `id` | string | Referenced by later steps and by field `path`s as `<id>:<pointer>`. |
| `type` | `"http"` (default) \| `"command"` | `http` issues a request; `command` runs a local process and parses its stdout as JSON. |
| `url` | template string | `http` only. |
| `method` | string | `http` only. Defaults to `GET`. |
| `headers` | object of string→template string | `http` only. |
| `auth` | object | `http` only. `{ "type": "basic", "user": "…", "password": "…" }`, `{ "type": "bearer", "value": "…" }`, or `{ "type": "header", "header": "X-Api-Key", "value": "…" }`. All of `user`/`password`/`value` are templates. |
| `body` | template string | `http` only. |
| `allowUntrustedCertificate` | boolean | `http` only. Accepts an invalid/self-signed TLS certificate for this request. |
| `commandLine` | template string | `command` only. |
| `stdin` | template string | `command` only, optional. |
| `timeoutMs` | number | Both. Defaults to `8000` when unset or ≤ 0. |
| `when` | template string | Run this step only if the template expands to a non-empty string. |
| `unless` | template string | Skip this step if the template expands to a non-empty string. |
| `optional` | boolean | A failing step is recorded and stepped over instead of ending the fetch. See [Optional steps](#optional-steps). |

Slack's manifest uses `when`/`unless` on a shared step `id` (`"message"`) to pick between a
top-level `conversations.history` call and a `conversations.replies` call, depending on whether
the link carried a `thread_ts`:

```jsonc
"fetch": [
    { "id": "message", "unless": "{{thread}}", "url": "…conversations.history…" },
    { "id": "message", "when": "{{thread}}",    "url": "…conversations.replies…" },
    { "id": "user",    "when": "{{message:/messages/-1/user}}", "url": "…users.info?user={{message:/messages/-1/user}}" }
]
```

Note the repeated `id`: two steps can share one id, and the later one that actually runs overwrites
the earlier result. That is what lets a `path` like `message:/messages/-1/text` resolve the same
way whichever branch fired.

##### Optional steps

The fetch pipeline normally stops at the first step that fails, and the card shows that error. A
step marked `"optional": true` is different: its failure is recorded and the pipeline carries on
without it. Anything that reads its result simply resolves to nothing, and the fields that depend
on it are skipped — an empty value never renders a row.

Reach for it in two situations:

- **An endpoint that may not exist.** Jira's `dev-status` API is undocumented and can return an
  error on any given site, so both of Jira's Development steps are optional. A site without it
  still gets a working card, just without the Development group.
- **A probe that is allowed to come back empty.** GitHub's `gh auth token` step is optional
  because "the CLI isn't installed" and "the CLI isn't signed in" are both ordinary outcomes, not
  failures.

##### Authenticating from a local CLI (the gh-then-token pattern)

A `command` step's stdout must parse as JSON, so a CLI that prints a bare token needs a wrapper.
GitHub's manifest runs one:

```jsonc
{
    "id": "ghtoken",
    "type": "command",
    "optional": true,
    "commandLine": "pwsh -NoProfile -NonInteractive -Command \"@{ token = (gh auth token 2>$null | Out-String).Trim() } | ConvertTo-Json -Compress\"",
    "timeoutMs": 4000
}
```

Everything after it comes in pairs that differ only in where the bearer token comes from, gated so
exactly one of each pair runs:

```jsonc
{ "id": "item", "when": "{{ghtoken:/token}}", "unless": "{{isissue}}{{sha}}",
  "url": "…/pulls/{{number}}", "auth": { "type": "bearer", "value": "{{ghtoken:/token}}" } },
{ "id": "item", "when": "{{ispull}}",         "unless": "{{ghtoken:/token}}",
  "url": "…/pulls/{{number}}", "auth": { "type": "bearer", "value": "{{credentials.token}}" } }
```

Three details make this work:

- **`when` is a presence test, not an expression.** There is no `and`, so the two conditions are
  split across the two slots a step has: the CLI variant puts "a CLI token exists" in `when` and
  the *wrong* link kinds in `unless`; the token variant puts the *right* link kind in `when` and
  "a CLI token exists" in `unless`.
- **Concatenation is an `or`.** `"{{isissue}}{{sha}}"` is non-empty when the link was an issue *or*
  a commit, because only one matcher fires per hover and the other groups expand to nothing. The
  same trick builds one URL out of two mutually exclusive values:
  `…/commits/{{sha}}{{item:/head/sha}}/check-runs` uses the hovered commit's sha for a commit link
  and the pull request's head sha for a pull link.
- **A marker group is just a named group over a literal.** GitHub's matchers capture
  `(?<ispull>pull)` and `(?<isissue>issues)` from the path purely so later steps have something to
  test. An earlier step's *result* works as a discriminator too: `{{item:/head/sha}}` is present
  only for pull requests, which is how the reviews step knows not to run for an issue.

If the CLI is missing, `pwsh` is missing, or the user is signed out, the probe step fails, it is
optional so the fetch continues, and `{{ghtoken:/token}}` stays empty — which is exactly the
condition the stored-token variants wait for. Nothing has to detect the failure explicitly.

#### Display fields

| Key | Type | Meaning |
|---|---|---|
| `key` | string | Identifies the field for the user's field-selection list. Defaults to `label` if omitted. |
| `label` | string | Shown next to the value. |
| `path` | JSON pointer | Where in a fetch step's result this field's value lives. See [Paths](#paths-json-pointers). |
| `kind` | see below | How the value renders. |
| `iconPath` | JSON pointer | A small (≤ 24 px) icon shown with the value. |
| `colorPath` | JSON pointer | A JSON pointer to a color value, for `badge` fields. |
| `color` | string | A literal color (`"#rrggbb"` or a name — see below), for `badge` fields. It is the **fallback**: `colorPath` is resolved first, and `color` is used only when that pointer yields nothing. |
| `format` | `"relativeTime"` \| `"date"` \| unset | Formats the raw value: `relativeTime` turns an ISO 8601 timestamp into "3 h ago"; `date` renders it as a date. |
| `default` | boolean | Shown by default before the user picks a custom field set. |

`kind` values: `text`, `title` (bold, first line), `subtitle`, `badge` (a colored pill), `link`,
`image` (≤ 24 px, from `iconPath`), `multiline` (up to 6 lines).

A field whose value resolves to an empty string is **not rendered at all** — no label, no row. That
is what lets one field list serve several link shapes: GitHub's `additions`/`deletions` fields sit
in the manifest unconditionally and simply vanish on an issue link, which has no such numbers.

Badge colors are matched case-insensitively. `#rrggbb` is taken literally; otherwise a small set of
names is recognized, chosen to cover Jira's `statusCategory.colorName` values and the obvious status
words:

| Renders as | Names |
|---|---|
| Green | `green`, `success`, `done`, `live`, `running`, `active` |
| Amber | `yellow`, `inprogress`, `in progress`, `warning`, `waiting`, `idle` |
| Red | `red`, `error`, `failed`, `blocked`, `dead`, `stale` |
| Blue | `blue`, `info`, `new`, `open` |
| Gray | anything else, including Jira's `blue-gray` |

#### Field groups

A `fieldGroups` entry names a set of display fields that belong together. The card and the pane
render them under the group's label, and the Integrations settings page gives the group a tri-state
header checkbox that selects or clears all of its fields at once.

| Key | Type | Meaning |
|---|---|---|
| `key` | string | Identifies the group. |
| `label` | string | Heading shown on the card and in settings. |
| `fields` | array of string | Display field `key`s, in the order the group shows them. |

```jsonc
"fieldGroups": [
    { "key": "details",     "label": "Details",     "fields": [ "summary", "status", "assignee", "updated" ] },
    { "key": "development", "label": "Development", "fields": [ "devBranches", "devCommits", "devPullRequests" ] }
]
```

Grouping is presentation only: a field's `key` still has to appear in `fields`, and a field named by
no group falls into an implicit "Details" group. A group whose fields all resolve to empty values
does not render.

#### Tabs

A tab is secondary content — too long for a field row — shown behind a compact tab strip above the
field list. Two shapes:

- **`"kind": "body"`** renders one value as a block of text. `path` points at that value.
- **`"kind": "list"`** repeats an author/avatar/body/time row over an array. `path` points at the
  array, and the four `item*Path` pointers are relative to *each element* of it.

| Key | Type | Meaning |
|---|---|---|
| `key` | string | Identifies the tab for the user's tab-selection list. |
| `label` | string | The tab's caption. |
| `kind` | `"body"` (default) \| `"list"` | One long value, or a repeating list. |
| `path` | JSON pointer | `body`: the value. `list`: the array to repeat over. |
| `format` | `"markdown"` \| `"adf"` \| `"text"` | How to interpret the body text. `markdown` goes through the Terminal's Markdown renderer; `adf` is [Atlassian Document Format](https://developer.atlassian.com/cloud/jira/platform/apis/document/structure/), a JSON document that is flattened to text; `text` is used verbatim. |
| `itemAuthorPath` | JSON pointer | `list` only, relative to each element. |
| `itemAvatarPath` | JSON pointer | `list` only, relative to each element. |
| `itemBodyPath` | JSON pointer | `list` only, relative to each element. |
| `itemTimePath` | JSON pointer | `list` only, relative to each element. |
| `default` | boolean | Shown by default before the user picks a custom tab set. |

```jsonc
"tabs": [
    {
        "key": "description", "label": "Description", "kind": "body",
        "path": "issue:/fields/description", "format": "adf", "default": true
    },
    {
        "key": "comments", "label": "Comments", "kind": "list",
        "path": "issue:/fields/comment/comments", "format": "adf",
        "itemAuthorPath": "/author/displayName",
        "itemAvatarPath": "/author/avatarUrls/24x24",
        "itemBodyPath": "/body",
        "itemTimePath": "/created",
        "default": false
    }
]
```

When the array a `list` tab needs *is* the whole result of a step — GitHub's issue-comments endpoint
returns a bare JSON array — point at the step with an empty pointer: `"path": "comments:"`. An empty
JSON pointer means "the whole document", per RFC 6901.

#### Actions

An action is something the card can do to the thing behind the link, rather than something it
reads. Two kinds:

- **`"kind": "button"`** fires one request.
- **`"kind": "choice"`** offers a list of options that an earlier fetch step produced, and fires a
  request for whichever one the user picks. The chosen option's id is available to the request
  template as `{{choice}}`.

| Key | Type | Meaning |
|---|---|---|
| `key` | string | Identifies the action. |
| `label` | string | Shown on the control. Defaults to `key`. |
| `kind` | `"button"` (default) \| `"choice"` | |
| `optionsPath` | JSON pointer | **Choice only.** The array of options, in a fetch step's result. |
| `optionIdPath` | JSON pointer | Relative to one option: the value `{{choice}}` becomes. |
| `optionLabelPath` | JSON pointer | Relative to one option: what to show in the list. |
| `optionBadgePath` | JSON pointer | Relative to one option: a badge shown beside the label — the state this option leads to. |
| `optionColorPath` | JSON pointer | Relative to one option: the badge's color. |
| `optionTargetIdPath` | JSON pointer | Relative to one option: the id of the state this option moves the thing *to*. |
| `currentStatePath` | JSON pointer | Where the thing's *current* state id lives. With `optionTargetIdPath`, this is what lets an undo find the option that leads back. |
| `optionFieldsPath` | JSON pointer | Relative to one option: fields the far end demands before it will accept the change. When the chosen option declares any, the card shows a small form and waits for them. |
| `method` | string | Defaults to `POST` (a fetch step defaults to `GET`). |
| `url` | template string | |
| `body` | template string | |
| `headers` | object of string→template string | |
| `auth` | object | Same shape as a [fetch step's](#fetch-steps). |
| `allowUntrustedCertificate` | boolean | |
| `timeoutMs` | number | Defaults to `8000`. |

##### Worked example: the Jira transition picker

Jira's transitions are a list of moves out of the issue's current status, each with a name, the
status it leads to, and sometimes a set of fields that must be filled in before Jira will accept it
(the "pick a resolution" dialog). One fetch step gets the list:

```jsonc
{
    "id": "transitions",
    "type": "http",
    "method": "GET",
    "optional": true,
    "url": "https://{{settings.host}}/rest/api/3/issue/{{key}}/transitions?expand=transitions.fields",
    "auth": { "type": "basic", "user": "{{credentials.email}}", "password": "{{credentials.token}}" },
    "headers": { "Accept": "application/json" }
}
```

and one action turns it into a control:

```jsonc
{
    "key": "transition",
    "label": "Move to",
    "kind": "choice",

    // Where the options are, and how to read one of them.
    "optionsPath": "transitions:/transitions",
    "optionIdPath": "/id",                                   // -> {{choice}}
    "optionLabelPath": "/name",                              // "Start progress"
    "optionBadgePath": "/to/name",                           // "In Progress"
    "optionColorPath": "/to/statusCategory/colorName",       // "yellow"
    "optionTargetIdPath": "/to/id",
    "optionFieldsPath": "/fields",                           // Jira's required-field form

    // Where the issue's current status id is, so an undo can look for the
    // transition whose /to/id leads back to it.
    "currentStatePath": "issue:/fields/status/id",

    // What to send once the user presses Apply.
    "method": "POST",
    "url": "https://{{settings.host}}/rest/api/3/issue/{{key}}/transitions",
    "body": "{\"transition\":{\"id\":\"{{choice}}\"}}",
    "auth": { "type": "basic", "user": "{{credentials.email}}", "password": "{{credentials.token}}" },
    "headers": { "Accept": "application/json", "Content-Type": "application/json" }
}
```

The card renders each option as `<name> → <badge>`, the way Jira's own menu does. Applying one runs
the request and refreshes the preview, bypassing the cache, so the status badge updates in place.

Undo is offered **only when the far end provides a way back** — that is, when some other transition
in the list has an `optionTargetIdPath` equal to the `currentStatePath` value from before the
change. Jira workflows are often one-directional, so this frequently isn't available; the card says
so rather than pretending.

Values the user typed into a required-field form are merged into the request as a top-level
`"fields"` object alongside what `body` declares, and are individually available to the template as
`{{field.<key>}}`.

#### detectPatterns

`detectPatterns` is a list of ICU regexes the terminal scans plain output for while the plugin is
enabled. Each match becomes hoverable exactly as if a text rule had matched it, which is how a
scheme the plugin owns works even when nothing marked it as a link:

```jsonc
"detectPatterns": [ "stith://(?:session|focus)/[A-Za-z0-9-]{4,64}" ]
```

Without this, `stith://session/abc123` printed as plain text is just text; with it, hovering shows
the same preview an OSC 8 hyperlink to the same URI would. The patterns join the same pool the
built-in URL and file-path detectors and any text rules use, so the **16 active text patterns**
limit covers them too.

This is distinct from a `"kind": "text"` [matcher](#matchers). A text matcher describes a pattern
the plugin *can* preview and offers it on the settings page as "Add as rule" — the user opts in. A
`detectPatterns` entry needs no rule and no opt-in beyond enabling the plugin, so it belongs to
schemes the plugin unambiguously owns, not to loose patterns like a bare issue key.

### Templates

Every templated string (`url`, `body`, headers, auth values, `commandLine`, `stdin`, `when`,
`unless`, a matcher's `link`) is expanded with `{{…}}` substitutions:

| Template | Expands to |
|---|---|
| `{{match}}` | The whole matched text. |
| `{{<name>}}` | A named capture group from the matcher's pattern, e.g. `{{key}}`. |
| `{{uri}}` | The hovered link's full URI (link matchers). |
| `{{settings.<key>}}` | A configured setting's value. |
| `{{credentials.<key>}}` | A stored credential's value. |
| `{{<stepId>:<json-pointer>}}` | A value from an earlier fetch step's JSON result. |
| `{{choice}}` | **[Actions](#actions) only.** The id of the option the user picked. |
| `{{field.<key>}}` | **[Actions](#actions) only.** A value the user typed into the action's required-field form. |

Values substituted into a `url` are URL-encoded.

**An unknown name expands to nothing** — not to an error, and not to the literal `{{name}}`. That
is deliberate, and it is what makes `when`/`unless` work as presence tests: a capture group that
belongs to a matcher which didn't fire, or a step that didn't run, simply leaves an empty string
behind.

### Paths (JSON pointers)

`path`, `iconPath`, and `colorPath` are [RFC 6901](https://www.rfc-editor.org/rfc/rfc6901) JSON
pointers into a fetch step's result:

- With a single fetch step, an unqualified pointer (`/fields/summary`) points into that step's
  result.
- With multiple steps, qualify with the step id: `message:/messages/-1/text`. An unqualified
  pointer falls back to **whichever step ran last and produced something**, which is rarely the one
  you meant — Jira's fields all say `issue:/…` for exactly this reason.
- An **empty** pointer after the step id (`comments:`) means the step's whole result, which is how
  a tab reads an endpoint that returns a bare JSON array.
- `-1` as an array index means **the last element** — Slack's manifest uses
  `message:/messages/-1/text` because `conversations.history`/`conversations.replies` return the
  message as the last (and often only) entry in a `messages` array.

## Host guarding

A link matcher's `hostSetting` names one of the plugin's own settings; the hovered URI's host must
equal that setting's **current, configured** value or the matcher does not fire at all — no
fetch, and no credential is touched. This is what stops a look-alike link
(`https://evil.example/browse/ABC-1`) from ever seeing a Jira token: the fetch simply never
starts, because the matcher never matched in the first place.

`hostSetting` is for a plugin whose host the *user* supplies. Where the host is fixed, pin it in the
pattern instead: GitHub's matchers begin `^https://github\.com/`, and every one of its fetch steps
addresses `api.github.com` literally, so there is no configured value a look-alike link could ever
match against.

More generally, a plugin's fetch pipeline never runs unless:

1. The plugin is **enabled** on the Integrations page, and
2. Every setting/credential marked `required` (or otherwise needed by the templates in use) has a
   value.

A plugin with nothing marked `required` — GitHub, whose token is optional because the CLI is tried
first — is therefore always considered configured, and hovering a matching link fetches as soon as
the plugin is enabled.

Otherwise the card shows a muted "not configured" line. Secrets never appear in `settings.json` —
see [Where credentials live](#where-credentials-live) — so sharing or backing up `settings.json`
never leaks a token.

## Caching

Each plugin's `cacheSeconds` controls how long a resolved preview is kept per matched
text/link — a second hover of the same link within that window skips the fetch entirely. GitHub,
Jira and Slack default to 300 seconds; Stith, whose data changes quickly, defaults to 30. Errors are
cached briefly too, so a broken credential doesn't retry on every hover.

Running an [action](#actions) refreshes that entry rather than reading it, so a status the user just
changed is never served stale from the cache.

## HTML representation (preview, off by default)

A manifest's `html` key is a full HTML document, given inline as a string (there is no
file-reference form). When the feature is enabled, the card renders this document in a WebView2
control instead of the plain `fields` list.

The page runs in a locked-down host: all navigation except the initial load is blocked, and there
is no context menu, no dev tools, and no zoom. Two things are injected before the page loads:

- `window.__data` — an object keyed by fetch step `id`, holding each step's JSON result (the same
  data `path`/`iconPath`/`colorPath` pointers resolve against for a `fields` card).
- `window.__uri` — the URI or text that was hovered.

The page has two ways to talk back to the host, both through `chrome.webview.postMessage`:

- `{ "height": <number> }` — resize the card to fit the content, clamped to **40–320 px**.
- `{ "open": "https://…" }` — open a link through the Terminal's normal link handling, the same
  path Open/Copy link use for a `fields` card.

Because the host applies its color scheme from the app theme, an `html` page should declare
`color-scheme: light dark` in its CSS rather than assuming one theme.

```jsonc
// integration.json
"html": "<!doctype html><html><head><meta charset='utf-8'><style>:root{color-scheme:light dark}body{font:12px system-ui;margin:6px}</style></head><body><div id='s'></div><script>const d=window.__data.issue.fields;document.getElementById('s').textContent=d.summary+' — '+d.status.name;function report(){chrome.webview.postMessage({height:document.body.scrollHeight})}window.addEventListener('load',report);new ResizeObserver(report).observe(document.body);document.body.addEventListener('click',e=>{if(e.target.tagName==='A'){e.preventDefault();chrome.webview.postMessage({open:e.target.href})}});</script></body></html>"
```

**In this build, `html` is compiled in but disabled.** Rendering it requires both the
`Feature_HyperlinkPreviewHtml` feature flag (`src/features.xml`) to be enabled and
`WebView2Loader.dll`, plus the WebView2 Runtime, to be present next to the app. Neither is true
today, so every plugin that sets `html` falls back to its `fields` list — don't rely on `html`
being rendered yet.

## Adding a third-party plugin

Drop a new folder under:

```
%LOCALAPPDATA%\Microsoft\Windows Terminal\Integrations\<your-plugin-id>\integration.json
```

and it's picked up the next time the registry refreshes (a settings reload triggers this). Use
the [Jira example](#full-annotated-example-jira) above as a starting template; a matcher, at least
one fetch step, and a small `fields` list is the whole surface area — `fieldGroups`, `tabs`,
`actions` and `detectPatterns` are all optional additions on top of that. Giving a user plugin the
same `id` as `github`, `jira`, `slack`, or `stith` replaces that built-in outright — useful for
forking a shipped plugin or handing ownership of it to another project's own repo.
