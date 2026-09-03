# Link previews and integration plugins

Hovering a link (or a piece of plain text that a rule recognizes) in the terminal can show a
card with live information pulled from an external tool — a Jira issue's summary and status, a
Slack message's author and text, a Stith session's name and state. This is driven by
**integration plugins**: small JSON manifests that describe how to recognize a match, how to
fetch data for it, and how to display the result. Three ship built in: **Jira**, **Slack**, and
**Stith**.

This document covers the feature end to end: what ships out of the box, the Integrations
settings page, text-pattern matching, the plugin manifest format for anyone writing their own,
and the security rules that keep credentials from leaking.

## What a link preview is

Hover a hyperlink (or a text match — see below) that a plugin recognizes, and the hyperlink card
grows a section below the usual link target: an icon and name for the plugin, then a small set of
fields fetched from that tool. The card shows a loading state while the fetch is in flight, and
caches the result for the plugin's configured lifetime so a second hover is instant.

If the plugin isn't configured (missing a required setting or credential), the card shows a muted
"`<Plugin>`: not configured" line instead of attempting a fetch. If a fetch fails, the card shows
the error inline (for example, "`Jira: 401 Unauthorized`") — it stays usable, just without a
preview.

## Built-in plugins

| Plugin | Recognizes | Needs | Notes |
|---|---|---|---|
| **Jira** | `https://<host>/browse/<KEY>` links, and (opt-in) issue keys like `CAB-8209` in plain text | Site host (setting) + account email and API token (credentials) | Credentials are only ever sent to the configured host — see [Host guarding](#host-guarding). Create an API token at `id.atlassian.com` → Security → API tokens. |
| **Slack** | `https://<workspace>.slack.com/archives/<channel>/p<ts>` permalinks, including thread replies (`?thread_ts=`) | A bot token (credential) | The token needs the `channels:history`, `groups:history`, and `users:read` scopes, and the bot must be a member of the channel it's reading. |
| **Stith** | `stith://session/<id>`, `stith://focus/<id>`, and `https://<server>/(s\|agent\|sessions\|embed/s)/<id>` links | Server URL (setting) | No credentials. The fetch allows an untrusted certificate, so a self-signed `lvh.me` cert doesn't block the preview. |

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
  in what order. Leaving all of them unchecked falls back to the manifest's own defaults.
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
        "fields": [ "summary", "status", "assignee", "updated" ]
    }
}
```

- `enabled` — whether the plugin is used for matching and preview at all.
- `settings` — the plugin's non-secret setting values, keyed by the manifest's setting `key`.
- `fields` — the display field keys to show, in order. Omit it (or leave it unset) to use the
  manifest's own `"default": true` fields.

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
            "url": "https://{{settings.host}}/rest/api/3/issue/{{key}}?fields=summary,status,assignee,priority,issuetype,updated,reporter",
            "auth": { "type": "basic", "user": "{{credentials.email}}", "password": "{{credentials.token}}" },
            "headers": { "Accept": "application/json" },
            "timeoutMs": 8000
        }
    ],

    // The card's field list. "default": true fields show unless the user picks a
    // different set on the Integrations page.
    "fields": [
        { "key": "summary",  "label": "Summary",  "path": "/fields/summary",              "kind": "title",  "default": true },
        { "key": "status",   "label": "Status",   "path": "/fields/status/name",          "kind": "badge",  "colorPath": "/fields/status/statusCategory/colorName", "default": true },
        { "key": "assignee", "label": "Assignee", "path": "/fields/assignee/displayName", "kind": "text",   "iconPath": "/fields/assignee/avatarUrls/24x24", "default": true },
        { "key": "priority", "label": "Priority", "path": "/fields/priority/name",        "kind": "text",   "iconPath": "/fields/priority/iconUrl", "default": false },
        { "key": "type",     "label": "Type",     "path": "/fields/issuetype/name",       "kind": "text",   "iconPath": "/fields/issuetype/iconUrl", "default": false },
        { "key": "reporter", "label": "Reporter", "path": "/fields/reporter/displayName", "kind": "text",   "iconPath": "/fields/reporter/avatarUrls/24x24", "default": false },
        { "key": "updated",  "label": "Updated",  "path": "/fields/updated",              "kind": "text",   "format": "relativeTime", "default": true }
    ]

    // "html": "…" — reserved for a later phase; see "HTML representation" below.
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
| `html` | string | Reserved; see [HTML representation](#html-representation-reserved). |

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

#### Display fields

| Key | Type | Meaning |
|---|---|---|
| `key` | string | Identifies the field for the user's field-selection list. Defaults to `label` if omitted. |
| `label` | string | Shown next to the value. |
| `path` | JSON pointer | Where in a fetch step's result this field's value lives. See [Paths](#paths-json-pointers). |
| `kind` | see below | How the value renders. |
| `iconPath` | JSON pointer | A small (≤ 24 px) icon shown with the value. |
| `colorPath` | JSON pointer | A JSON pointer to a color value, for `badge` fields. |
| `color` | string | A literal color (`"#rrggbb"` or a Jira-style name like `blue-gray`/`yellow`/`green`), for `badge` fields. Takes precedence when both `color` and `colorPath` could apply. |
| `format` | `"relativeTime"` \| `"date"` \| unset | Formats the raw value: `relativeTime` turns an ISO 8601 timestamp into "3 h ago"; `date` renders it as a date. |
| `default` | boolean | Shown by default before the user picks a custom field set. |

`kind` values: `text`, `title` (bold, first line), `subtitle`, `badge` (a colored pill), `link`,
`image` (≤ 24 px, from `iconPath`), `multiline` (up to 6 lines).

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

Values substituted into a `url` are URL-encoded.

### Paths (JSON pointers)

`path`, `iconPath`, and `colorPath` are [RFC 6901](https://www.rfc-editor.org/rfc/rfc6901) JSON
pointers into a fetch step's result:

- With a single fetch step, an unqualified pointer (`/fields/summary`) points into that step's
  result.
- With multiple steps, qualify with the step id: `message:/messages/-1/text`.
- `-1` as an array index means **the last element** — Slack's manifest uses
  `message:/messages/-1/text` because `conversations.history`/`conversations.replies` return the
  message as the last (and often only) entry in a `messages` array.

## Host guarding

A link matcher's `hostSetting` names one of the plugin's own settings; the hovered URI's host must
equal that setting's **current, configured** value or the matcher does not fire at all — no
fetch, and no credential is touched. This is what stops a look-alike link
(`https://evil.example/browse/ABC-1`) from ever seeing a Jira token: the fetch simply never
starts, because the matcher never matched in the first place.

More generally, a plugin's fetch pipeline never runs unless:

1. The plugin is **enabled** on the Integrations page, and
2. Every setting/credential marked `required` (or otherwise needed by the templates in use) has a
   value.

Otherwise the card shows a muted "not configured" line. Secrets never appear in `settings.json` —
see [Where credentials live](#where-credentials-live) — so sharing or backing up `settings.json`
never leaks a token.

## Caching

Each plugin's `cacheSeconds` controls how long a resolved preview is kept per matched
text/link — a second hover of the same link within that window skips the fetch entirely. Jira and
Slack default to 300 seconds; Stith, whose data changes quickly, defaults to 30. Errors are
cached briefly too, so a broken credential doesn't retry on every hover.

## HTML representation (reserved)

The manifest schema reserves an `"html"` key for a richer, HTML/CSS/JS-rendered card in place of
the plain field list. It is not implemented yet — any manifest that sets it is ignored until that
phase ships, and the plugin falls back to its `fields` list. Don't rely on `html` being rendered.

## Adding a third-party plugin

Drop a new folder under:

```
%LOCALAPPDATA%\Microsoft\Windows Terminal\Integrations\<your-plugin-id>\integration.json
```

and it's picked up the next time the registry refreshes (a settings reload triggers this). Use
the [Jira example](#full-annotated-example-jira) above as a starting template; a matcher, at least
one fetch step, and a small `fields` list is the whole surface area. Giving a user plugin the same
`id` as `jira`, `slack`, or `stith` replaces that built-in outright — useful for forking a shipped
plugin or handing ownership of it to another project's own repo.
