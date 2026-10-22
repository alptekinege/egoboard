# Usage

## Shortcuts

| Keys | Action |
|------|--------|
| `Meta+V` | Quick-paste popup (number keys `1–9` paste directly) |
| `Meta+Shift+V` | Show/hide the history window |
| `Meta+Shift+D` | Delete the newest entry |
| `Meta+Shift+P` | Pause/resume capture |
| `Ctrl+K` | Command palette (`>tag`, `>export`, `>transform`, `>dashboard`, `>profile`, …, or plain text to search) |
| `Ctrl+F` or `/` | Focus the search field |
| `Alt+1…5` | Focus search / list / preview / groups / timeline |
| `Enter` | Paste the selected entry · `Esc` clears search, then filters, then closes |
| `?` | Shortcut cheatsheet |

Global shortcuts are registered with KGlobalAccel and work while Egoboard is hidden; the Shortcuts settings page resets them to defaults.

## Search syntax

Type plain words (every word must match; prefix matching with diacritics folded), plus field filters: `app:`, `type:`, `tag:`, `pinned:`, `sensitive:`, `has:ocr`, `before:`, `after:`. Quote phrases (`"exact words"`); `-word` excludes; `OR`/`AND`/`NOT` combine. The search box explains each typed filter live, and committed queries are remembered as recent searches.

## Snippets, transforms & scripts

- **Snippets** (`>snippet` or Settings ▸ Automation): templates expanded with the selected entry or clipboard text — `{{clipboard}}`/`{{text}}`/`{{selection}}`, `{{date}}`, `{{time}}`, `{{datetime}}`, `{{timestamp}}` — optionally bound to a global shortcut.
- **Transforms** (`>transform`): 17 built-in operations (case, Base64/URL/HTML encode/decode, JSON formatting, line sorting/uniquing, …) plus chained transforms and user JavaScript actions run in a sandbox with a 2 s timeout. Apply them to the selected entry or the current clipboard.

## Privacy & encryption

- **Sensitive data** (Settings ▸ Privacy): `Exclude` never stores matches (default on shared machines), `Mark` stores but flags, `Redact` stores with secrets replaced by `••••` (per-kind toggles + custom regex + live tester).
- **Per-app ignore** with `*`/`?` wildcards (password managers belong here) plus a live tester.
- **Encryption at rest** (opt-in SQLCipher build, key in KWallet): tick the checkbox and Apply; the database is rekeyed in place.
- **Previews**: sensitive entries blur until hovered/focused; everything blurs automatically while the screen is shared (status-bar indicator).

## Backups, import/export, profiles

- **Backups** (`Settings ▸ Storage`): *Back up now (.zip)* writes one archive containing the database, settings, and an integrity manifest. Without libzip/SQLite snapshot support the same contents go into an atomic multi-file `.backup` folder instead. Restore validates the archive before touching anything and offers merge or overwrite (with a confirmation); cancel keeps the current history and writes no partial package. Settings ▸ Diagnostics shows which backup format the build uses.
- **Automatic backups**: optional daily backups with a retention count, in the same format as manual backups.
- **Export**: JSON (round-trip), Markdown, CSV, HTML, and image folders with a manifest. Sensitive entries are skipped unless you explicitly opt in.
- **Import**: JSON (merge, overwrite, or skip duplicates), Klipper (`history3.sqlite`), and CopyQ `*.cpq` exports. CopyQ imports show the tabs and item counts first; entries are deduplicated by content hash, and unencrypted v1–v4 exports are read while password-protected v5 exports are refused with a re-export hint. Every import rolls back on cancel.
- **Settings portability**: Export/Import settings as JSON next to the history backup (encryption keys stay in KWallet; backup-schedule state stays local).
- **Profiles** (`Work`, `Personal`, …): save whole setting sets in Settings ▸ Storage and switch from the palette with `>profile <name>`.

## Dashboard

Settings ▸ Usage (or `>dashboard`) shows read-only local aggregates over the existing database: activity per day, entries by content type, top source apps, a size histogram, and the current streak. Sensitive entries appear only as counts, never as text, and the charts are keyboard-focusable. Nothing leaves the machine and no extra dependency is needed — the charts are painted with Qt Widgets, not QtCharts.

## KRunner & D-Bus

The `eb ` KRunner trigger searches history with per-match actions (paste/copy/pin/delete) and content-type result categories. The same surface is scriptable over the session bus (`org.egoboard.Egoboard` at `/org/egoboard/Egoboard`): `Search`/`SearchDetailed`/`Preview`/`Paste`/`Copy`/`Pin`/`Delete`, e.g. `qdbus org.egoboard.Egoboard /org/egoboard/Egoboard org.egoboard.Egoboard.Search hello 5`.

## Platform notes

- **X11**: active-window tracking via `_NET_WM_PID`; paste via XTest key injection (`xdotool` fallback).
- **Wayland**: focus-free capture via `ext-data-control` (current KWin) / `wlr-data-control` (wlroots compositors) where the compositor exposes either (QClipboard fallback, focused-window only); paste shows a “press Ctrl+V” notification — or, opt-in, asks the compositor to press it through the desktop portal (Settings ▸ General ▸ Pasting; Plasma prompts for permission on first use; the notification fallback always stays).
- **Screencast**: best-effort PipeWire detection (optional build input) drives the status indicator + preview auto-blur; without it nothing changes.
- **OCR** needs `tesseract` (+ language data); without it images simply carry no recognized text.

## Diagnostics

Settings ▸ Diagnostics shows platform state (layer-shell, data-control, portal availability), database pragmas/size, backup format availability (zip / SQLite snapshot / JSON fallback), and a copy-pasteable report (config + counts, never clipboard contents). `--crash-report` / `--read-crash-report` collect and render structured, redacted crash bundles; Settings ▸ Storage ▸ Check integrity runs `PRAGMA quick_check` with a persistent, actionable error state.
