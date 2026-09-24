<p align="center">
  <img src="data/egoboard.svg" width="128" alt="Egoboard icon">
</p>

# Egoboard

Clipboard history manager for KDE Plasma. Keeps everything you copy, lets you search it, and paste it back with one keypress. Works on X11 and Wayland. Local-only: SQLite database, no accounts, no network, no telemetry.

## Features

- **History** — stores text, HTML, images, and file copies in SQLite (WAL mode, deduplicated, virtualized list stays instant at 50k+ entries).
- **Search & filter** — full-text search with field filters, plus type/date/app/tag/sort presets, saved searches, and removable filter chips.
- **Pins & groups** — pin favorites, organize them into nested groups (drag & drop onto groups).
- **Shortcuts** — `Meta+V` opens quick-paste, `Meta+Shift+V` toggles the window.
- **Privacy** — auto-detects credit cards, passwords, and API keys; flag, exclude, or redact them.
- **Auto-cleanup** — rule-based expiry by age and type, plus disk quota.
- **Export/Import** — backup and restore as JSON.
- **Themes** — follows your active Plasma color scheme and icon theme, or pick any installed KDE scheme; text size and colors are adjustable when a theme is hard to read.

## Use

### Shortcuts

| Keys | Action |
|------|--------|
| `Meta+V` | Quick-paste popup (number keys `1–9` paste directly) |
| `Meta+Shift+V` | Show/hide the history window |
| `Meta+Shift+D` | Delete the newest entry |
| `Meta+Shift+P` | Pause/resume capture |
| `Ctrl+K` | Command palette (`>tag`, `>group`, `>export`, `>profile`, `>tour`, …) |
| `Ctrl+F` or `/` | Focus the search field |
| `Alt+1…5` | Focus search / list / preview / groups / timeline |
| `Enter` | Paste the selected entry · `Esc` clears search, then filters, then closes |
| `?` | Shortcut cheatsheet |

Global shortcuts are registered with KGlobalAccel and work while Egoboard is hidden; the Shortcuts settings page resets them to defaults.

### Search syntax

Type plain words (every word must match; prefix matching with diacritics folded), plus field filters: `app:`, `type:`, `tag:`, `pinned:`, `sensitive:`, `has:ocr`, `before:`, `after:`. Quote phrases (`"exact words"`); `-word` excludes; `OR`/`AND`/`NOT` combine. The search box explains each typed filter live, and committed queries are remembered as recent searches.

### Privacy & encryption

- **Sensitive data** (Settings ▸ Privacy): `Exclude` never stores matches (default on shared machines), `Mark` stores but flags, `Redact` stores with secrets replaced by `••••` (per-kind toggles + custom regex + live tester).
- **Per-app ignore** with `*`/`?` wildcards (password managers belong here) plus a live tester.
- **Encryption at rest** (opt-in SQLCipher build, key in KWallet): tick the checkbox and Apply; the database is rekeyed in place.
- **Previews**: sensitive entries blur until hovered/focused; everything blurs automatically while the screen is shared (status-bar indicator).

### Backups, import/export, profiles

- **Automatic backups**: optional daily JSON backups with retention count (`Settings ▸ Storage`), plus manual back up / restore with cancelable progress.
- **Export/Import**: JSON, Markdown, CSV, HTML, and image-folder (+ manifest) exports; JSON and Klipper (`history3.sqlite`) imports — all cancelable, imports roll back on cancel.
- **Settings portability**: Export/Import settings as JSON next to the history backup (encryption keys stay in KWallet; backup-schedule state stays local).
- **Profiles** (`Work`, `Personal`, …): save whole setting sets in Settings ▸ Storage and switch from the palette with `>profile <name>`.

### KRunner & D-Bus

The `eb ` KRunner trigger searches history with per-match actions (paste/copy/pin/delete) and content-type result categories. The same surface is scriptable over the session bus (`org.egoboard.Egoboard` at `/org/egoboard/Egoboard`): `Search`/`SearchDetailed`/`Preview`/`Paste`/`Copy`/`Pin`/`Delete`, e.g. `qdbus org.egoboard.Egoboard /org/egoboard/Egoboard org.egoboard.Egoboard.Search hello 5`.

### Platform notes

- **X11**: active-window tracking via `_NET_WM_PID`; paste via XTest key injection (`xdotool` fallback).
- **Wayland**: focus-free capture via `wlr-data-control` where the compositor exposes it (QClipboard polling fallback); paste shows a “press Ctrl+V” notification — or, opt-in, asks the compositor to press it through the desktop portal (Settings ▸ General ▸ Pasting; Plasma prompts for permission on first use; the notification fallback always stays).
- **Screencast**: best-effort PipeWire detection (optional build input) drives the status indicator + preview auto-blur; without it nothing changes.
- **OCR** needs `tesseract` (+ language data); without it images simply carry no recognized text.

### Diagnostics

Settings ▸ Diagnostics shows platform state (layer-shell, data-control, portal availability), database pragmas/size, and a copy-pasteable report (config + counts, never clipboard contents). `--crash-report` / `--read-crash-report` collect and render structured, redacted crash bundles; Settings ▸ Storage ▸ Check integrity runs `PRAGMA quick_check` with a persistent, actionable error state.

## [Build](./docs/build.md) · [Release checklist](./docs/release-checklist.md)

## License

MIT.
