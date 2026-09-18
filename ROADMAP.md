# Egoboard — Future Plan

> Local-first clipboard history for KDE Plasma. This roadmap balances what's already solid (SQLite + WAL history, X11/Wayland tracking, virtualized two-pane UI, groups & pins, FTS5 search, OCR, transforms, snippets) with what's worth adding next — without turning the app into a cloud service.

**Status:** `v0.1.0` · Qt 6 + KF6 · local-only · MIT · no compilation required for this document.

**Revision 2026-09-18:** **Phase 6 (correctness & hardening, Track G) and Phase 7 (Track H — Search 2.0) are delivered.** §4 and §5 are condensed to struck-through records with the handful of remaining optional items listed at the end of §4; §1.3 now describes what is actually left. Phase 8 (Track I/J — portability & integration) is the next phase; Track L (CI, packaging, docs) follows. Previous revision 2026-09-17: merged the prior roadmap (every entry preserved; delivered work compressed to struck-through single lines) with a full line-by-line review of `src/core`, `src/app`, `src/app/ui`, `tests/`, `docs/`, packaging and repo hygiene. Carried-over backlog: semantic search (Track A), LAN sync / browser companion (Track D), Settings & QoL (§3).

---

## 1. Where we are (reviewed 2026-09-17)

### 1.1 Architecture as built

```
src/
├── core/   egoboard_core (static lib, Qt6::Core + Qt6::Sql only)
│           StorageManager, DatabaseSchema, SearchEngine, TransformEngine,
│           SnippetManager, VacuumWorker, BookmarkManager, GroupTreeModel*,
│           ExportImportManager, SensitiveDataDetector, ExpirePolicy,
│           ClipboardListModel  (* compiled into the app target — QIcon)
├── app/    runtime & platform: ApplicationContext (composition root),
│           ClipboardWatcher, AutoPaster, HotkeyManager, TrayController,
│           SettingsManager, SingleInstanceGuard, ExpireScheduler,
│           ScriptActionManager (QJSEngine), EncryptionManager (SQLCipher+KWallet),
│           EgoboardDbusAdaptor, OcrWorker (tesseract), theme subsystem
│           (ThemeManager, ColorSchemeIndex, IconThemeManager, IconThemeIndex,
│           TextAppearance, SystemThemeWatcher), Wayland helpers
│           (LayerShellHelper, WlrDataControlHelper, KWinCursorTracker),
│           X11ActiveWindowTracker / WaylandActiveWindowTracker
├── app/ui/ MainWindow, EntryDelegate, PreviewPane, QuickPasteMenu, GroupsDock,
│           SettingsDialog (9 pages), ExportImportDialogs, CommandPalette,
│           TimelineStrip, SnippetDialog, TransformChainDialog,
│           AppearancePreview, CodePreviewHighlighter
├── krunner/ KRunner plugin (`eb ` trigger, D-Bus search → Paste)
└── main.cpp  entrypoint, --smoke headless self-test
```

### 1.2 What's solid

Unlimited history with content hashing & dedup, source-app tracking (EWMH on X11 / `wlr-foreign-toplevel` on Wayland), a virtualized list with keyset pagination, favorites & nested groups, tags, saved searches, sort modes, `KGlobalAccel` shortcuts (`Meta+V` quick paste / `Meta+Shift+V` toggle / `Meta+Shift+D` delete-last) plus `1–9` and `Ctrl+1–9` paste, FTS5 search with fuzzy `Ctrl+K` palette, Tesseract OCR, code/link/color previews, transform chains (built-in + sandboxed JS), snippets, per-app ignore rules, capture-type filters, entry/disk retention caps, redaction + SQLCipher encryption at rest, rule-based expiry, JSON export/import, timeline strip, D-Bus automation API, theme discovery that follows the user's Plasma colors/icons (no bundled theme assets; assets are runtime-discovered and never modified by the app).

The architecture is clean: `egoboard_core` stays `QtCore+Sql` only, platform seams go through `IClipboardStorage` / `IActiveWindowTracker`, and `ApplicationContext` is the composition root. 23 test files run headless (`QTEST_GUILESS_MAIN` + `QTemporaryDir`).

### 1.3 What needs work

Phase 6 (hardening) and Phase 7 (Search 2.0) are delivered, so the fundamentals are no longer the gap. What remains is portability and integration work plus a few documented leftovers:

* **Track I — data portability & durability** (first batch delivered): ~~scheduled backups~~ ✅ and ~~startup integrity (`quick_check` + index repair)~~ ✅; still open: importers for CopyQ/Klipper exports, a dedicated restore flow, `.zip` archives, and a pre-migration file copy.
* **Track J — integration 2.0**: `xdg-desktop-portal` RemoteDesktop paste on Wayland, global snippet hotkeys (the stored `snippet.shortcut` is still unbound), KRunner/palette/tray upgrades, pause-capture on lock/screencast.
* **Track K — workflow delight** (multi-select, undo toast, paste queue, stats) and **Track L — release engineering** (CI pipeline, Flatpak/AUR, screenshots).
* **Phase 5 leftover**: Quick paste 2.0 (search-as-you-type inside the popup, two-line previews, multi-monitor memory).
* **§4 tail**: qtkeychain fallback (plan only), wlr-data-control mime reads still happen on the GUI thread (bounded by a shared read budget), import/export shows a wait cursor but no progress dialog, and the app/UI layer still has thinner test coverage than `src/core`.
* Long-term: semantic search (Track A), LAN sync + browser companion (Track D).

### 1.4 Guiding principles for every future feature

1. **Local-first, private by default.** No account, no telemetry, no network call without explicit opt-in.
2. **Performance is a feature.** History must stay instant at 50k+ entries.
3. **No core pollution.** `src/core` stays GUI-free and testable headless.
4. **Wayland parity.** Anything that works on X11 must have a Wayland-respectful equivalent (notification/portal > injection).
5. **Harden before widen.** Phase 6 correctness fixes land before new features build on the same surfaces.
6. **Accessible by default.** Every interactive surface reachable by keyboard with a proper accessible name.

---

## 2. Delivered work — archive (condensed)

Everything below shipped; kept for the record as single lines.

**Phases 1–4b**
* ~~Phase 1 — Foundation: FTS5 migration, fuzzy command palette (`Ctrl+K`), code-aware preview, timeline strip.~~
* ~~Phase 2 — Understanding: OCR worker (Tesseract), link & color previews, per-app ignore rules.~~
* ~~Phase 3 — Automation: transform actions (sandboxed JS engine), snippet templates, KRunner plugin.~~
* ~~Phase 4 / 4b — Platform: layer-shell quick-paste popup (Wayland), optional `wlr-data-control` capture helper.~~

**Feature tracks**
* ~~Track A: fuzzy command palette + FTS5 full-text search (`unicode61`, prefix queries).~~
* ~~Track B: image OCR, code-aware previews, link & color previews.~~
* ~~Track C: built-in transform chain, snippet templates, per-app ignore rules, sandboxed JS scripting hook.~~
* ~~Track D: layer-shell quick-paste (Wayland), `wlr-data-control` capture, KRunner plugin (`eb ` trigger).~~
* ~~Track E: redaction mode with per-kind toggles, SQLCipher encryption at rest (KWallet-held key), auto-expire rules, audit view.~~
* ~~Track F: timeline / calendar strip above the list; global search widget (KRunner).~~

**Settings & QoL batches (§3 items already delivered)**
* ~~List density (compact / comfortable / spacious).~~
* ~~Relative timestamps ("2 h ago" vs absolute) + 12/24 h clock (Appearance controls, applied live).~~
* ~~Sort modes: newest / oldest / most used — keyset pagination is mode-aware and persisted.~~
* ~~Geometry memory (window size/position + splitter) and opt-in last-filter restore.~~
* ~~Saved searches ("smart folders"): save/apply/delete filter combinations.~~
* ~~Tags: user labels, case-insensitive unique names, tag filter combo, right-click "Tags ▸".~~
* ~~Paste behavior settings: close-after-paste, bump-to-top, always-paste-as-plain-text.~~
* ~~"Paste as" submenu: plain text / UPPERCASE / lowercase / with timestamp / image → PNG file.~~
* ~~Number-key paste: `Ctrl+1…9` from the main window.~~
* ~~Global delete-last hotkey (`Meta+Shift+D`, pinned entries protected).~~

**Also shipped since the previous roadmap (not previously listed)**
* ~~Capture-type filters; entry-count retention cap; configurable debounce; per-app ignore list.~~
* ~~KDE color-scheme + icon-theme support with system theme watching; text size/color overrides with WCAG contrast floors and a live appearance preview.~~
* ~~Autostart entry self-healing with absolute executable paths.~~
* ~~KWin global cursor anchoring for the quick-paste popup (Wayland).~~
* ~~Settings dialog rebuilt with an icon sidebar; storage/diagnostics pages; D-Bus automation seam (`Search`, `Paste`, `ShowQuickPaste`, `ReportCursorPos`, `Ping`).~~
* ~~Shortcut default swap (`Meta+V` → quick paste, `Meta+Shift+V` → toggle).~~

**Phase 6 — Correctness & hardening (Track G) — delivered 2026-09-18**
* ~~G1 data layer: `fetchAll` keyset cursor carries `useCount` (MostUsed no longer truncates at 500 rows); dedup touch is monotonic and refreshes payload/flags/OCR ("latest copy wins", pins preserved); `removeEntries`/`clearHistory` emit only real changes; `addSavedSearch` is a true upsert on `name`; `VacuumWorker` resets its handle and uses a per-run connection name.~~
* ~~G1 schema: indexes for `use_count`, `entry_groups(group_id)` and pinned/sensitive partials; legacy BLOB `content_hash` rows normalized to TEXT; `saved_searches.name` unique `COLLATE NOCASE`; `ExpirePolicy` rejects out-of-range content types; `BookmarkManager` group names unique per parent (case-insensitive) with transactional `deleteGroup`; `GroupTreeModel::parent` walks parent pointers.~~
* ~~G2 runtime: tray Settings/Clear History wired; stale-lock recovery restored (60 s) and listen failures surfaced; JS transforms run under a watchdog thread (arrow functions supported, cached source); CSPRNG database key; wallet results surfaced; **encryption lifecycle** — key applied before the schema, in-place `PRAGMA rekey` enable/decrypt flow with confirmations and status, startup reconciles setting ↔ disk; KWin script unload/cleanup; wlr-data-control capture-type gating + offer destruction + shared read budget; Wayland toplevel reset on global withdrawal; token-based clipboard self-suppression; X display reuse and paste-failure notifications.~~
* ~~G3 UI: `Ctrl+K` registered once; `>pin`/`>copy` implemented (palette emits `copyRequested`); TimelineStrip hover + per-bar tooltip; PreviewPane widget-based pages and guards; `useCount` badge matches the stored count; Settings storage link fixed, FTS tester explains the query with hit count and timing, shortcuts page scrolls, import/export shows a wait cursor; accessible names/descriptions across list, search, palette, quick paste and dialogs; KRunner launches via KService/`findExecutable` (AppImage-friendly) with translated strings; dead declarations removed.~~
* ~~G4 scale: `ClipboardListModel` patches rows incrementally instead of resetting per capture; tray menu rebuilds lazily from summary rows; imports run as one transaction with suppressed per-row signals and `fetchAllFull` pages payloads in single queries (export 6.1 s → 3.9 s at 50k); `--bench[=N]` synthetic scale harness with budgets (insert, page, deep paging, most-used, FTS, export); `idx_entries_use_count` added.~~
* ~~G5 privacy: export/import round-trips OCR, tags, snippets and saved searches and `Overwrite` clears everything it replaces; one shared sensitive-scan path for both capture sources.~~
* ~~G6 hygiene: LICENSE (MIT); `docs/build.md` dependency set completed (`qt6-declarative`, Wayland tooling, krunner, kwallet+sqlcipher, libxtst, layer-shell-qt, flags); `agent.md` module map brought in line with `src/`; settings schema versioning (`[General] ConfigVersion`, forward-only migrations with tests); `--smoke` wipes its scratch DB so runs are repeatable.~~

**Phase 7 — Search 2.0 (Track H) — delivered 2026-09-18**
* ~~Field filters in the search box and palette: `app:`, `type:`, `tag:`, `pinned:`, `sensitive:`, `has:ocr`, `before:`/`after:` (ISO dates, `today`/`yesterday`, `30m/12h/3d/2w`), quoted values, unknown fields left as text; typed fields override the toolbar presets and the applied set is shown as a hint.~~
* ~~Query syntax: quoted phrases become real FTS5 phrases, `-term` and uppercase `NOT` exclusions, uppercase `OR` alternatives (AND binds tighter), and a guarded `/regex/` mode validated at parse time — matched client-side per scope with a 20k-row scan cap, 200-row batches and a 20k-character subject cap per field.~~
* ~~Search highlight: matched ranges drawn in the list delegate and marked in the text preview, palette-aware (selection-aware colors, no hardcoded values).~~
* ~~Recent searches: up to 10 committed queries (Enter, menu pick, or paste while searching) with a clear action, persisted in `egoboardrc`.~~
* ~~Scope selector: preview / full text / OCR / all text, persisted, part of saved searches and mapped to FTS5 column filters (LIKE fallback picks the same columns).~~
* ~~Search stats + explain: the settings tester shows parsed filters, rejected values, the resulting FTS5 expression, the hit count and elapsed milliseconds.~~

**Phase 8 — Portability & integration (Track I, first batch) — delivered 2026-09-18**
* ~~**Automatic backups**: daily JSON export into a configurable folder (default `~/Documents/egoboard-backups`), "keep the newest N" pruning, a manual "Back up now" that also works with the schedule off, and an immediate first backup when the feature is enabled. The export runs on a worker thread with its own database connection (`BackupService`/`BackupWorker`), failures raise a notification, and the settings dialog shows the last run.~~
* ~~**Startup integrity**: one-shot background `PRAGMA quick_check` on a scratch connection after start — silent when healthy, notifying with guidance when not — plus "Check integrity" and "Rebuild search index" actions in Storage settings, and `StorageManager::rebuildSearchIndex()` as the safe repair path.~~
* ~~**Tests**: backup write/prune/name-ordering + import-back round trip (`tst_exportimport`), service + worker thread end to end (`tst_backupservice`), quick-check and index-repair (`tst_schema`).~~

**Packaging & verification (2026-09-18)**
* ~~AppImage rebuilt from the hardened tree and passed `scripts/validate-appimage.sh`: desktop/icon/metadata checks, Qt platform plugins (XCB + Wayland), SQLite driver, SVG formats, 198 bundled shared libraries, and a headless `--smoke` run inside the AppImage.~~
* ~~`--smoke` / `--bench` now force console logging, so their reports stay visible when the output is piped or captured (the AppImage validator greps for them).~~

---

## 3. Outstanding backlog carried over from the previous roadmap

### Track A — Intelligence & Search

* ~~Fuzzy command palette (`Ctrl+K`) and FTS5 full-text search (`unicode61` tokenizer, prefix queries) — delivered (Phase 1).~~
* **Optional local semantic search** (opt-in, ONNX-quantized embedding, e.g. `all-MiniLM-L6-v2` via `onnxruntime`). No network. Embeddings stored alongside entries; cosine search for "that postgres error from last week".

```cpp
// core/SearchEngine.h — FTS5 always available; semantic path only if enabled
class SearchEngine : public QObject {
    Q_OBJECT
public:
    QVector<qint64> searchFts(const QString &query, int limit) const;
    QVector<ScoredId> searchSemantic(const QString &query, int k) const;
    bool isSemanticAvailable() const;
};
```

### Track B — Content Understanding

* ~~Image OCR (local Tesseract), code-aware previews, link & color previews — delivered (Phase 2).~~
* **Smart title generation**: `2024-08-23 14:02 — copied from Kate` → `Fix dedup hash in StorageManager` when the text is a commit message.

### Track C — Workflow Automation

* ~~Built-in transform chain, snippet templates, per-app ignore rules, sandboxed JS scripting hook — delivered (Phase 3).~~
* Next up: favorite transforms, extended snippet placeholders, per-app auto-actions (see §3.6).

### Track D — Platform & Integration

* ~~Layer-shell quick-paste (Wayland), `wlr-data-control` capture, KRunner plugin (`eb ` trigger) — delivered (Phase 4).~~
* **KDE Connect / LAN sync** (opt-in, E2E-encrypted): sync pinned snippets across devices on the same network using a pairing code — never a central server. History sync stays off by default.
* **Browser companion** (optional WebExtension, local WebSocket on `127.0.0.1`): copy code blocks with one click, preserve source URL.

### Track E — Privacy & Security Hardening

* ~~Redaction mode with per-kind toggles, SQLCipher encryption at rest (KWallet-held key), auto-expire rules, audit view — delivered.~~

### Track F — UX Polish

* ~~Timeline / calendar strip above the list; global search widget (KRunner) — delivered.~~
* **Inline diff for dedup**: "same hash as 2h ago — updated `use_count`".
* **Accessibility**: full keyboard navigation, screen-reader labels, high-contrast delegate.

### 3.1 Capture & history

* **Pause capture** — tray menu entry + global shortcut; optional auto-pause while a fullscreen app is active (presentations, games).
* **Noise filter** — skip whitespace-only, single-character or very short copies (`minTextLength`, default 0).
* **Ignore private windows** — never record from incognito/private browser windows.
* **Clipboard ↔ selection sync** — optional X11 mode that mirrors clipboard to primary selection.
* ~~**Auto-backups** — daily JSON export into a configurable folder, keep last N — delivered with Phase 8 (`BackupService`, worker thread, pruning, manual "Back up now").~~
* **Move history DB** — override the database path from Settings (with a safe move + reopen flow).

### 3.2 List & reading

* **List density** — compact / comfortable / spacious row heights. *(delivered in Batch 1)*
* ~~**Relative timestamps** — "2 h ago" vs absolute; 12/24 h clock — delivered ("Timestamps" combo + clock checkbox in Appearance, applied live).~~
* **Row extras** — optional entry index, use-count badge, "pasted today" dot.
* ~~**Sort modes** — newest first (default) / oldest first / most used — delivered (sort combo in the filter bar; keyset pagination is mode-aware; choice persists).~~
* **Single-click paste** — optional; double-click stays the default activator.
* ~~**Geometry memory** — remember window size/position and splitter ratio; restore last filter on start — delivered (geometry/splitter always remembered; last filter is opt-in).~~
* ~~**Saved searches ("smart folders")** — pin a filter combination (e.g. "unpinned images from GIMP") — delivered ("Searches" menu in the filter bar: apply, save current filter, delete).~~
* ~~**Tags** — user labels per entry with case-insensitive unique names, tag filter combo, right-click "Tags ▸" submenu — delivered; palette `>tag` support and filter chips still pending.~~
* ~~**Search highlight + recent searches** — matched ranges marked in the list delegate and preview; recent-query menu with a clear action; delivered with Track H (Phase 7).~~

### 3.3 Pasting

* ~~Paste behavior settings — close window after paste (default on), bump pasted entry to top (default on), global "always paste as plain text" — delivered.~~
* ~~"Paste as" submenu — plain text, UPPERCASE, lowercase, with timestamp, image → PNG file — delivered (list context menu).~~
* ~~Number-key paste — `Ctrl+1…9` pastes the first nine visible entries from the main window — delivered.~~
* ~~Global delete-last hotkey — drop the most recent capture without opening the window (default `Meta+Shift+D`, pinned entries protected) — delivered.~~
* **Quick paste 2.0** — search-as-you-type inside the popup, optional two-line previews, multi-monitor placement memory. *(bigger effort — not yet implemented)*

### 3.4 Privacy

* **Privacy blur** — redact Mark-mode previews in the list until hovered.
* **Auto-lock** — hide/lock history after N minutes idle; unlock with a local PIN (offline only).
* **Screenshare awareness** — blur previews while the compositor reports an active screencast (best effort).

### 3.5 Preview

* **Image zoom & pan** — wheel zoom, fit/100 % toggle.
* **Inline edit** — fix typos before pasting; write-back re-hashes and re-indexes the entry.
* **Split & merge** — split one entry by lines into many; merge the selection into one entry.
* **Color & QR details** — big swatch with RGB/HSL values; QR-encode URLs using a local library only.

### 3.6 Automation

* **Favorite transforms** — pin often-used transforms to the top of the Transform menu.
* **Extended snippet placeholders** — `{{selection}}`, `{{app}}`, `{{window}}`, `{{date:format}}`.
* **Per-app auto-actions** — auto-uppercase / auto-redact / auto-tag rules tied to the source app (extends the per-app rules engine).

### 3.7 App & settings UX

* **Settings search** — a filter box atop the dialog that jumps to matching pages and highlights matching knobs.
* **Per-page "Reset to defaults"** + settings export/import (JSON, next to the history backup).
* **Profiles** — switch whole setting sets ("Work" / "Personal") from the palette.
* **Tray interactions** — configurable left-click action (window / quick paste / menu); wheel over the tray icon cycles recent entries.
* **i18n** — ship a translation catalog (KDE l10n); `tr()` extraction is already in place.
* **First-run tour** — 4-step overlay: hotkeys, palette, privacy modes, settings search.

---

## 4. Phase 6 — Correctness & hardening (Track G) — ✅ delivered (2026-09-18)

> Every G1–G6 defect from the 2026-09-17 review is fixed; the detailed archive is in §2. The leftovers are deliberate, not forgotten:

* **Plan only**: a qtkeychain-backed fallback for the deprecated KWallet framework.
* **Mitigated, not removed**: wlr-data-control mime reads still run on the GUI thread — capture-type gating, a single shared read budget and payload-free early exits bound the stall; moving protocol I/O off the QPA thread needs its own design.
* **Optional / out of scope**: pause capture while the session is locked or a screencast is active; a real progress dialog for import/export (a wait cursor ships today).
* **Coverage**: `src/core` is well covered (storage, schema/search, export/import, list model, expiry, snippets, transforms, scripts, vacuum, encryption, bookmarks, group tree, settings migrations); the app/UI layer has widget tests but still no `ApplicationContext` integration test.
* **CI**: a GitHub Actions workflow was added and then removed by the maintainer; the item is carried into Track L (§5) rather than re-added here.

### Delivered, by area

* ~~**G1 — Data layer** (`src/core`): keyset-cursor fix, dedup monotonicity + payload refresh, signals that reflect real changes, true saved-search upsert, vacuum connection hygiene, index/schema normalization (`use_count`, `entry_groups(group_id)`, pinned/sensitive partials, `content_hash` typing, NOCASE saved searches), expiry validation, bookmark duplicates + transactional delete, group-tree parent pointers, dead-code/doc cleanup.~~
* ~~**G2 — Runtime & platform** (`src/app`): tray wiring, single-instance stale-lock recovery, JS transform watchdog (arrow functions, cached source), CSPRNG key + full encryption lifecycle (key before schema, in-place `PRAGMA rekey`, decrypt flow, status/notifications), KWin script lifecycle, wlr gating/offer destruction, Wayland toplevel reset, token-based clipboard self-suppression, X display reuse + paste-failure signal, live quick-paste count, retention timer, OCR default alignment.~~
* ~~**G3 — UI & accessibility**: one `Ctrl+K` registration, palette `>pin`/`>copy`, timeline hover + tooltip, preview-page/delegate fixes, settings storage link + query tester + scrolling, wait cursor for import/export, accessible names across list/search/palette/quick-paste/dialogs, KRunner launch that works for AppImage installs, removed dead declarations.~~
* ~~**G4 — Performance & scale**: incremental list-model updates, lazy tray menu from summary rows, single-transaction imports with one refresh, single-query full fetches, `--bench[=N]` budgets (insert, page, deep paging, most-used, FTS, export).~~
* ~~**G5 — Privacy & security**: export/import round-trips OCR + tags + snippets + saved searches, `Overwrite` clears everything it replaces, one shared sensitive scan for both capture paths.~~
* ~~**G6 — Repo & docs**: LICENSE (MIT), refreshed `docs/build.md` and `agent.md`, settings `ConfigVersion` migrations, repeatable `--smoke` scratch DB.~~

---

## 5. New feature tracks (beyond the carried-over backlog)

### Track H — Search 2.0 (all offline) — ✅ delivered (2026-09-18)

* ~~**Field filters in the search box / palette**: `app:`, `type:`, `tag:`, `pinned:`, `sensitive:`, `has:ocr`, `before:`/`after:` — mapped onto `FilterSpec`, overriding the toolbar presets, with a live "filtering by …" hint and rejected values called out.~~
* ~~**Query syntax**: quoted phrases (real FTS5 phrases), `-term`/`NOT` exclusion, uppercase `OR` alternatives, and the guarded `/regex/` mode (parse-time validation, clear errors, capped scan + batch + subject length).~~
* ~~**Search highlight**: matched ranges marked in the list delegate and the text preview, palette-aware.~~
* ~~**Recent searches** menu + a **scope selector** (preview / full text / OCR / all); both persisted, scope is part of saved searches.~~
* ~~**Search stats + query explain**: the settings-dialog tester shows the parsed filters, the FTS5 expression, the hit count and elapsed time.~~
* Semantic search stays long-term (Track A).

### Track I — Data portability & durability

* ~~**Export format v2**: OCR text, tags, snippet library and saved searches round-trip; group memberships exported; versioned with a reader that still imports v1 files — delivered with Phase 6 (G5).~~
* ~~**Import completeness**: `Overwrite` clears all user data it replaces; one transaction with batched (suppressed) signals and a single refresh — delivered with Phase 6 (G4/G5). A cancelable progress UI is still open (wait cursor today).~~
* **More export formats**: Markdown / HTML / CSV, plus "export selection" from multi-select (Track K).
* **Importers for other managers**: CopyQ JSON export, Klipper history file — all funneled through the existing content-hash dedup path so imports merge cleanly.
* ~~**Scheduled backups** (from §3.1) — delivered with Phase 8 (`BackupService`: daily, configurable folder, keep-N pruning, manual run). Still open: a dedicated restore flow (importing the JSON works today) and an optional compressed (`.zip`) archive.~~
* ~~**Startup integrity** — delivered with Phase 8: `PRAGMA quick_check` after start (background, notify-on-failure), "Check integrity" + "Rebuild search index" actions, and `rebuildSearchIndex()`. Still open: an automatic pre-migration file copy.~~

### Track J — Desktop integration 2.0

* **Wayland auto-paste via `xdg-desktop-portal` RemoteDesktop**: opt-in, per-session consent, real keystroke delivery instead of the notification-only fallback. This is the missing piece of full Wayland parity.
* **Global snippet hotkeys** (Track C/§3.6-adjacent): bind snippet shortcuts through `KGlobalAccel`, finishing the stored-but-unbound `shortcut` field.
* **KRunner improvements**: ~~configurable launch command (AppImage-friendly) — delivered with Phase 6 (G3): KService/`findExecutable` launch~~; still open: richer results (app/type/source, pinned marker), extra actions (pin, copy, delete) and live preview.
* **Palette commands**: ~~`>pin`, `>copy` — delivered with Phase 6 (G3)~~; still open: `>delete`, `>tag`, `>group`, `>export`, `>pause`, `>settings`, `>clean` with argument completion and recent-command memory.
* **Tray/system**: configurable left-click, wheel-to-cycle recent (from §3.7), plus pause-capture and lock/screencast awareness (§3.1/§3.4).

### Track K — Workflow delight

* **Multi-select & bulk actions**: delete / pin / tag / move to group / export for a selection (Shift/Ctrl ranges).
* **Undo toast** for destructive actions (delete entry, bulk delete, clear history, expire sweep) backed by a soft-delete window.
* **Paste queue**: enqueue multiple entries (Ctrl+click) and paste them in order, one keystroke per paste.
* **Group-by-day headers** (sticky date separators) as an alternative list mode.
* **Local statistics dashboard**: top sources, entries/day, storage growth, sensitive-hit counts, OCR coverage; CSV export.
* **Regex watch rules**: user patterns that auto-tag or mark matching captures (extends custom sensitive patterns and per-app rules).
* **Snippet fill-in placeholders**: prompt for `{{name}}` values at insert time; snippet categories/tags.
* **Named transform presets**: save a chain under a name and bind it to a hotkey (extends §3.6 "favorite transforms").
* **Active-filter chips**: one-click removable chips for the current filter set (finishes the pending tag/palette piece of §3.2).

### Track L — Release engineering & CI

* **CI pipeline** as in G6; add a nightly AppImage build + `validate-appimage.sh` run.
* **LICENSE (MIT)** plus an SPDX header policy for new files.
* **Packaging beyond AppImage** (optional, AppImage stays primary): Flatpak manifest with a KDE runtime, and a distro template (AUR PKGBUILD).
* **Docs set**: refreshed `build.md`, corrected `agent.md` module map, plus `ARCHITECTURE.md` and `CONTRIBUTING.md`; screenshots/GIFs in the README.
* **Settings schema versioning** (G6) and **benchmark budget gate** in CI (G4).

---

## 6. Phased delivery (updated)

Keep it iterative. Each phase ships and is usable on its own — no big-bang rewrite.

| Phase | Focus | Key deliverables |
|-------|-------|------------------|
| **Phase 1 — Foundation** ✅ | Search + UX speed | FTS5 migration, fuzzy command palette (`Ctrl+K`), code-aware preview, timeline strip |
| **Phase 2 — Understanding** ✅ | Content smarts | OCR worker (Tesseract), link/color previews, per-app ignore rules |
| **Phase 3 — Automation** ✅ | Daily-driver loop | Transform actions (sandboxed JS), snippet templates, KRunner plugin |
| **Phase 4 — Platform** ✅ | Wayland & capture | Layer-shell popup, optional `wlr-data-control` helper |
| **Phase 5 — Ergonomics** ◐ | Settings & QoL | §3 backlog: paste behavior ✅, list density ✅, saved searches & tags ✅; Quick paste 2.0 still open |
| **Phase 6 — Hardening** ✅ | Correctness first | G1–G5 defects, G4 index + signal fixes, G6 LICENSE/docs — all delivered 2026-09-18 (§4) |
| **Phase 7 — Search & scale** ✅ | Retrieval | Track H (field filters, phrases/AND-OR/NOT, guarded regex, highlight, recent searches, scope, query explain) + `--bench` budgets — delivered 2026-09-18 (§5) |
| **Phase 8 — Portability & integration** ◐ *in progress* | Backup & platform | Track I first batch ✅ (automatic backups, startup integrity, index repair); open: CopyQ/Klipper importers, `.zip` backups, Track J (portal paste, snippet hotkeys, KRunner/palette/tray) |
| **Phase 9 — Release & delight** | Polish | Track L (CI, Flatpak, docs), Track K (multi-select, undo, paste queue, stats) |
| **Tracks A / D** | Long-term | Semantic search (Track A), LAN sync + browser companion (Track D) |

> Phases are sequential by default, but §3 batches and Track K items can be reordered freely by need — every batch ships independently.

---

## 7. Suggested immediate fixes (prioritized) — ✅ all landed

The P0–P2 list from the 2026-09-17 review is implemented (details in §2 and the per-area summary in §4). The single exception is the CI workflow: it was added, then removed by the maintainer, and now lives in Track L.

| Priority | Item | Status |
|----------|------|--------|
| **P0** | Connect tray `settingsRequested` / `clearRequested` | ✅ delivered |
| **P0** | Fix `fetchAll` keyset cursor (`useCount`) — silent 500-row truncation on `MostUsed` | ✅ delivered (+ regression test) |
| **P0** | Reset `QSqlDatabase` before `removeDatabase` in the vacuum worker; per-run connection name | ✅ delivered |
| **P0** | Export/import round-trip parity (OCR, tags, snippets, saved searches; clear-all on Overwrite) | ✅ delivered |
| **P0** | Restore stale-lock recovery in the single-instance guard | ✅ delivered (60 s window) |
| **P1** | JS transform execution timeout (matches the documented budget) | ✅ delivered (watchdog thread) |
| **P1** | CSPRNG key generation + encryption error surfacing + rekey lifecycle | ✅ delivered |
| **P1** | KWin script unload/cleanup lifecycle | ✅ delivered |
| **P1** | wlr-data-control: capture-type gating, offer destruction, off-thread reads | ✅ gating + offer destruction + shared read budget; off-thread reads still open (see §4) |
| **P1** | Add missing indexes (`use_count`, `entry_groups(group_id)`, pinned/sensitive) — additive migration | ✅ delivered |
| **P1** | Dedup timestamp policy + payload refresh on touch | ✅ delivered ("latest copy wins") |
| **P2** | Single `Ctrl+K` registration; implement or remove `>pin`/`>copy`; TimelineStrip hover | ✅ delivered |
| **P2** | Accessibility names for delegate/list/popup/dialogs + Settings storage link fix | ✅ delivered |
| **P2** | LICENSE, `docs/build.md` deps, `agent.md` module map | ✅ delivered (CI → Track L) |

---

## 8. Architecture notes (staying clean)

* New modules respect the existing boundary: `SearchEngine` and `TransformEngine` already live in `egoboard_core` (headless-testable; a separate `egoboard_transform` lib remains an option if the surface grows). OCR, layer-shell, wlr-data-control, KWin scripting and any future networking stay in `src/app`.
* Migrations are additive and forward-only: index-only changes, FTS5 table + triggers, a nullable `entry_embeddings` table when semantic search lands, plus a settings `ConfigVersion`. No breaking change to `history.db`, never drop user data.
* All heavy work stays off the GUI thread: `QThreadPool` for OCR, `QtConcurrent` for embeddings, the `VacuumWorker` pattern for compaction. Export/import batching was fixed in Phase 6; wlr-data-control payload reads are still on the GUI thread but gated and budgeted (§4).
* Feature flags live in `SettingsManager` (`KConfig` groups) so OCR, semantic search, encryption, scripts and sync can be disabled independently.
* No bundled color schemes or icons: theme and icon themes are discovered at runtime from the system (`ColorSchemeIndex`, `IconThemeIndex`) and pre-existing theme assets are treated as read-only — the app never ships, rewrites or overwrites them.
* New settings/features are wired end-to-end in one pass: `SettingsManager` key → enforcement point → live UI reaction to `changed()` → diagnostics entry → tests → this roadmap.

---

## 9. Non-goals (intentionally not doing)

* No cloud account, no central server, no telemetry.
* No AI that sends clipboard content to an external API.
* No Electron / rewrite — Qt Widgets + KF6 stays.
* No automatic history upload; sync is pairwise + E2E if ever enabled.

---

## 10. Working locally

This roadmap is a **document only** — no build, no CI, no compiled artifact of its own. To iterate on it locally:

```bash
# edit this file, preview in any markdown viewer
xdg-open ROADMAP.md

# or just keep it in the repo root for visibility
git log --oneline -- ROADMAP.md
```

Feedback loop for code work: pick one batch, implement, then run the standard gate:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/egoboard --smoke
```

(then move to the next batch). The plan stays in version control so it evolves with the code.

---

*Last updated: 2026-09-18 (Phase 6 + Phase 7 delivered; Phase 8 Track I first batch: automatic backups, startup integrity) · Maintainer: local development · Next review: after the next Phase 8 batch — remaining long-term: semantic search (Track A), browser/LAN sync (Track D), Track I/J/K/L items above.*
