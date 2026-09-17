# Egoboard — Future Plan

> Local-first clipboard history for KDE Plasma. This roadmap balances what's already solid (SQLite + WAL history, X11/Wayland tracking, virtualized two-pane UI, groups & pins, FTS5 search, OCR, transforms, snippets) with what's worth adding next — without turning the app into a cloud service.

**Status:** `v0.1.0` · Qt 6 + KF6 · local-only · MIT · no compilation required for this document.

**Revision 2026-09-17:** merged the previous roadmap (every entry preserved; delivered work compressed to struck-through single lines) with a full line-by-line review of `src/core`, `src/app`, `src/app/ui`, `tests/`, `docs/`, packaging and repo hygiene. New in this revision: **Phase 6 — Correctness & hardening (Track G)** from the review findings, plus new tracks **H (Search 2.0)**, **I (Data portability)**, **J (Desktop integration 2.0)**, **K (Workflow delight)**, **L (Release engineering & CI)**. Carried-over backlog: semantic search (Track A), LAN sync / browser companion (Track D), Settings & QoL (§3).

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

The review found the fundamentals sound but flagged real (mostly latent) defects, missing indexes, dead wiring, and thin coverage of the app/UI layer. The concrete list is §4; the prioritized hit list is §7. The headline gaps: tray Settings/Clear History buttons are dead, `fetchAll` silently truncates under `MostUsed`, JS transforms can hang the GUI with no timeout, export/import drops OCR text + tags + snippets + saved searches, `SingleInstanceGuard` can wedge on a stale lock after a crash, the wlr-data-control path ignores capture-type filters and reads clipboard payloads on the GUI thread, accessibility is essentially absent, there is no CI and no LICENSE file, and `docs/build.md` / `agent.md` have drifted from the code.

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
* **Auto-backups** — daily JSON export into a configurable folder, keep last N.
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
* **Search highlight + recent searches** — mark matched substrings in results; dropdown of previous queries.

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

## 4. Phase 6 — Correctness & hardening (Track G) — from the 2026-09 review

> Recommended next phase. These are real defects and gaps found in the current tree; each is small enough to ship independently. File references are to the reviewed revision (`092126e`).

### G1 — Data layer correctness (`src/core`)

* **`fetchAll` drops `useCount` from the keyset cursor** (`StorageManager.cpp:264`) → under `SortMode::MostUsed` the second page returns nothing and `fetchAll` silently truncates at 500 rows. Fix the cursor construction; add a per-sort-mode `fetchAll` regression test.
* **Dedup path rewrites `timestamp_ms` with the incoming (possibly older) timestamp** and never refreshes payload/flags (`StorageManager.cpp:39-126`) → a re-copied old item can jump *down* in Newest order. Define "latest copy wins", refresh `preview`/`text_data`/`sensitive`/`ocr_text` on touch, and clamp the timestamp monotonic.
* **`removeEntries` ignores the transaction result** and emits `entriesRemoved(ids)` even for rows that did not exist; `clearHistory` emits `storageReset` but no `entriesRemoved` → model/caches drift. Make signals reflect actual changes.
* **`addSavedSearch` uses `INSERT OR REPLACE` but documents "keeps the row id"** (`StorageManager.cpp:531-542`) → the returned id is a new row id. Implement a true upsert on `name` (UPDATE … WHERE name = :name) or correct the contract and callers.
* **`VacuumWorker` removes its QSqlDatabase connection while the handle is still in scope** (`VacuumWorker.cpp:31-34`) and uses a fixed connection name → Qt "connection is still in use" warning and concurrent-run clobbering. Adopt the StorageManager reset-then-remove pattern; use a per-run connection name.
* **Schema/index gaps** (additive migration):
  * no index for the `MostUsed` sort (`use_count`);
  * no index on `entry_groups(group_id)` (only the `(entry_id, group_id)` PK), so group lookups scan;
  * no index supporting `pinned` / `sensitive` filters;
  * `content_hash` is declared `TEXT` but bound as a BLOB — normalize the declaration/typing;
  * `saved_searches.name` is case-sensitive `UNIQUE` while queries use `COLLATE NOCASE` — align with `tags` (`UNIQUE COLLATE NOCASE`).
* **`ExpirePolicy::parseContentType` accepts out-of-range numerics** (`ExpirePolicy.cpp`) that then match nothing; validate and drop invalid rules.
* **Minor**: `BookmarkManager::deleteGroup` proceeds when `transaction()` fails and allows duplicate names; `GroupTreeModel::parent()` does an O(n) recursive scan; `GroupTreeModel.cpp` remains the lone QtGui include in `src/core` (documented exception, excluded from the core target).
* **Dead code / doc drift**: unused `StorageManager::exec` and `m_insertCounter` (its comment describes enforcement that isn't wired); `TransformEngine::htmlUnescape` unused iterator; `HtmlEscape` label claims `'`-escaping though `toHtmlEscaped()` does not escape it; `SnippetManager` header says placeholders are case-sensitive while the implementation is case-insensitive.

### G2 — Runtime & platform correctness (`src/app`)

* **Tray buttons are dead**: `TrayController::settingsRequested` / `clearRequested` are emitted (`TrayController.cpp:105,109`) but never connected in `ApplicationContext.cpp:224-228` → "Settings…" and "Clear History…" do nothing.
* **`SingleInstanceGuard` sets `setStaleLockTime(0)`** (`SingleInstanceGuard.cpp:21`) → after a crash the lock file is never considered stale and startup is blocked forever; a failed `listen()` is also ignored. Re-enable stale-lock recovery with a sane age and surface listen failures.
* **JS transform sandbox has no timeout**: `ScriptActionManager::apply` calls `transform()` directly (`ScriptActionManager.cpp:178`) while the header claims a "2 s logical timeout" (`ScriptActionManager.h:18`) → `while(true){}` freezes the GUI thread. Add interrupt/termination (worker thread or `QJSEngine` interrupt) and enforce the documented budget. `hasTransform = source.contains("function transform")` misses arrow-function actions; the script file is re-read on every apply (TOCTOU vs the cached list).
* **Encryption hardening**:
  * `EncryptionManager::generateKey` uses the non-CSPRNG `QRandomGenerator::global()` (`EncryptionManager.cpp:120`) — use a CSPRNG-backed source for the DB key;
  * `removeKey` ignores the wallet result and always reports Ok;
  * KWallet is a deprecated framework — plan a qtkeychain-backed fallback.
* **Encryption lifecycle**: the key is applied after `StorageManager` has already opened and initialized the database, so an existing plaintext DB cannot actually be encrypted, and key failures are `qWarning`-only with no user-visible state. Add a documented "encrypt existing DB" flow (reopen with key, `PRAGMA rekey`), plus explicit error/status reporting.
* **`KWinCursorTracker`**: the unload name may not match the loaded plugin name after a retry (`KWinCursorTracker.cpp:114-131`) → leaked KWin scripts; the temp JS file is never removed and uses a fixed, racy name. Track the exact plugin name, clean up on finish/failure.
* **`WlrDataControlHelper`**: ignores `captureText` / `captureImages` / `captureFiles` toggles (unlike `ClipboardWatcher`), never destroys `zwlr_data_control_offer_v1` objects on selection change (map/offer leak), and reads mime payloads with a blocking pipe poll on the GUI thread (up to ~1.2 s per mime). Gate by capture type, destroy offers, move reads off the GUI thread.
* **`WaylandActiveWindowTracker`** does not handle the manager's `finished` event → stale toplevel handles if the compositor withdraws the global.
* **`ClipboardWatcher`**: the fixed 2000 ms self-suppression window can swallow genuine rapid user copies; `suppressedOwnChange` is declared but never emitted; the forced `formats()` fetch is heuristic. Prefer an ownership token over a time window.
* **`AutoPaster`**: opens a new X display per XTest call; `failed` is declared but never connected; `copyToClipboard` duplicates the paste-set path.
* **`ApplicationContext`**: `quickPasteCount` changes are not applied live (the popup is built once, parentless) → stale until restart; retention caps are enforced only every 25th capture (a sub-25 burst never enforces); `OcrWorker` default `maxChars = 8000` disagrees with the settings default 8192.

### G3 — UI correctness & accessibility (`src/app/ui`)

* **`Ctrl+K` is bound three times** (toolbar action `MainWindow.cpp:260`, `QShortcut` `:311`, `keyPressEvent`) — collapse to one registration.
* **Command palette advertises unimplemented commands**: `>pin / >copy (soon)` (`CommandPalette.cpp:293`) are not implemented; `CommandPalette::copyRequested` is declared and connected (`MainWindow.cpp:879`) but never emitted; `m_query` is a dead member.
* **`TimelineStrip` hover is dead**: `m_hovered` is read in `paintEvent` (`TimelineStrip.cpp:116,130`) but never assigned (no `mouseMoveEvent` despite `setMouseTracking(true)`); the count-on-hover block is an empty stub and the tooltip promises hover info. Implement per-bar hover count/tooltip or remove the hover code.
* **`PreviewPane`**: image page selected via magic index (`setCurrentIndex(3)`, `PreviewPane.cpp:454`) — use `setCurrentWidget`; duplicate `#include`; redundant `wasTransformed` branches; `humanSize` has no guard for `<= 0`.
* **`EntryDelegate`** displays `useCount + 1` — align the label with the stored count or document the intent.
* **`MainWindow`**: `m_ignoreHideOnFocusOut` is declared but never used (`MainWindow.h:94`); `Escape`-to-hide and keyboard paste only work when the list does not have focus — route window-level shortcuts through one consistent path.
* **`SettingsDialog`**: the Storage page's database-path label has no `<a href>` anchor so the connected `linkActivated` never fires (`SettingsDialog.cpp:904-911`) — dead "open folder"; the FTS query tester shows tokenization only (no hit count); import/export run without progress UI; the Shortcuts page is the only non-scrolling page.
* **Snippets**: the `shortcut` field is stored but never bound ("not yet bound", `SnippetDialog.cpp:36`) — implement global snippet hotkeys (Track J) or drop the field.
* **Accessibility pass**: no `setAccessibleName`/`Description` anywhere in the reviewed UI files; the delegate, `TimelineStrip` and `QuickPasteMenu` convey state by color/number only; no `setBuddy`/mnemonics on dynamically built forms.
* **KRunner plugin**: launches the literal command `"egoboard"` (`EgoboardRunner.cpp`) → fails for AppImage installs; several user-facing strings are raw `QStringLiteral` (no `i18n`) — same for two diagnostics strings in `SettingsDialog.cpp:1254,1425`.
* **Dead declaration**: `ExportDialog::updatePath()` is declared (`ExportImportDialogs.h:49`) but never defined.

### G4 — Performance & scale

* **`ClipboardListModel` resets the whole model on every storage signal** (`entryAdded` / `entryTouched` / `entriesRemoved` / `storageReset`) → scroll position and selection are discarded on each capture. Insert/patch/remove incrementally; keep full reset for `storageReset` only.
* **Tray menu rebuilds call `fetchFull` per recent entry on every `entryAdded`** (`TrayController.cpp:64`) → per-capture cost. Cache previews from the summary record instead.
* **`ExportImportManager` is N+1** (`fetchAllFull` → `fetchFull` per id) and emits `entryAdded` per row plus a final `storageReset` during import → thousands of signals. Batch inserts in one transaction with silent mode, then a single refresh.
* **"Most used" sort has no index** (see G1) → full scan + sort; measure and add `idx_entries_use_count`.
* **Scale benchmark**: add a synthetic 100k-entry harness to `tests/` (or a `--bench` mode) with budgets for page fetch, FTS query, insertion and export; run `INSERT INTO entries_fts(entries_fts) VALUES('optimize')` after bulk import and revisit the WAL checkpoint policy.

### G5 — Privacy & security

* CSPRNG key generation, encryption error surfacing, qtkeychain fallback (see G2).
* Script sandbox execution limit + a documented capability statement (see G2).
* **Export/import completeness** (also Track I): export omits `ocr_text`, tags, snippets and saved searches; `Overwrite` import wipes only `entries` + `groups` (`ExportImportManager.cpp:232`), leaving stale tags/snippets/saved searches behind. A "self-contained backup" must round-trip *all* user data and clear what it claims to.
* Sensitive-mode parity on the wlr-data-control path (detection currently runs twice; capture-type gating missing) — one shared scan path for both capture sources.
* **Optional**: pause capture while the session is locked or a screencast is active (ties into §3.4).

### G6 — Repo, docs & release hygiene

* **No `LICENSE` file** despite README and the KRunner metadata declaring MIT — add it.
* **No CI at all** (no `.github/`, no `.gitlab-ci.yml`): add a workflow that installs the documented dependency set, builds, runs `ctest --output-on-failure`, runs `--smoke` under `QT_QPA_PLATFORM=offscreen`, and (on tags) builds + validates the AppImage.
* **`docs/build.md` is incomplete**: missing required `qt6-declarative` (Qt6 Qml is `REQUIRED`), the Wayland scanner/protocol tooling, optional `kf6-krunner`, `kf6-kwallet` + `sqlcipher`, `libxtst`, `layer-shell-qt`; no mention of `-DBUILD_TESTING` or `-DEGOBOARD_USE_SQLCIPHER`.
* **`agent.md` is stale**: its module tree describes `src/{storage,bookmarks,io,hotkeys,tray}/` directories that no longer exist, omits ~15 current modules, and its smoke command (`./build/src/egoboard --smoke`) does not match the actual binary (`./build/egoboard`).
* **Test coverage gaps**: no tests for `ApplicationContext`, `HotkeyManager`, `TrayController`, `KWinCursorTracker`, `X11/WaylandActiveWindowTracker`, the KRunner runner itself, and nearly all UI widgets (`MainWindow`, `EntryDelegate`, `PreviewPane`, `QuickPasteMenu`, `GroupsDock`, `SettingsDialog`, `CommandPalette`, `TimelineStrip`, dialogs). Add at least smoke-level widget tests and an `ApplicationContext` integration test.
* **Settings schema versioning**: there is no config version/migration framework (only ad-hoc fallbacks). Add `[General] ConfigVersion` + forward-only migrations with tests.
* **Asset policy**: the app ships no colors or icons of its own (only the app SVG); theme assets stay user/system-owned, are discovered at runtime and are never modified — keep it that way as the theme subsystem grows.

---

## 5. New feature tracks (beyond the carried-over backlog)

### Track H — Search 2.0 (all offline)

* **Field filters in the search box / palette**: `app:`, `type:`, `before:`/`after:`, `tag:`, `pinned:`, `has:ocr` — mapped onto the existing `FilterSpec` fields.
* **Query syntax**: quoted phrases (FTS5 already), `-term` exclusion, AND/OR, and an optional guarded regex mode (debounced, capped, clear error on invalid pattern).
* **Search highlight**: matched ranges marked in the list delegate and preview (extends the §3.2 item).
* **Recent searches** dropdown + a "scope" selector (preview / full text / OCR / all).
* **Search stats + query explain**: result count, elapsed time, and the token list produced by `SearchEngine::buildFtsQuery` (finishes the settings-dialog tester).
* Semantic search stays long-term (Track A).

### Track I — Data portability & durability

* **Export format v2**: include OCR text, tags, snippet library and saved searches; group memberships by path; versioned (`formatVersion: 2`), forward-compatible reader that still imports v1 files.
* **Import completeness**: `Overwrite` clears all user data it replaces; single transaction; batched signal emission; cancelable progress UI.
* **More export formats**: Markdown / HTML / CSV, plus "export selection" from multi-select (Track K).
* **Importers for other managers**: CopyQ JSON export, Klipper history file — all funneled through the existing content-hash dedup path so imports merge cleanly.
* **Scheduled backups** (from §3.1) with a restore flow and an optional compressed (`.zip`) archive.
* **Startup integrity**: `PRAGMA quick_check` on open, pre-migration backup copy, and a guided auto-repair path.

### Track J — Desktop integration 2.0

* **Wayland auto-paste via `xdg-desktop-portal` RemoteDesktop**: opt-in, per-session consent, real keystroke delivery instead of the notification-only fallback. This is the missing piece of full Wayland parity.
* **Global snippet hotkeys** (Track C/§3.6-adjacent): bind snippet shortcuts through `KGlobalAccel`, finishing the stored-but-unbound `shortcut` field.
* **KRunner improvements**: configurable launch command (AppImage-friendly), richer results (app/type/source, pinned marker), extra actions (pin, copy, delete) and live preview.
* **Palette commands**: implement `>pin`, `>copy`, and add `>delete`, `>tag`, `>group`, `>export`, `>pause`, `>settings`, `>clean` with argument completion and recent-command memory.
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
| **Phase 5 — Ergonomics** | Settings & QoL | §3 backlog in batches: paste behavior + list density first, then saved searches & tags, then Quick paste 2.0 |
| **Phase 6 — Hardening** ← *next* | Correctness first | G1–G3 defects (dead tray wiring, fetchAll cursor, vacuum connection, JS timeout, encryption key lifecycle, wlr-data-control gating), G4 index + signal fixes, G6 LICENSE/CI/docs |
| **Phase 7 — Search & scale** | Retrieval | Track H (field filters, regex, highlight, recent searches) + G4 benchmark budgets |
| **Phase 8 — Portability & integration** | Backup & platform | Track I (export v2, importers, integrity check), Track J (portal paste, snippet hotkeys, KRunner/palette/tray) |
| **Phase 9 — Release & delight** | Polish | Track L (CI, Flatpak, docs), Track K (multi-select, undo, paste queue, stats) |
| **Tracks A / D** | Long-term | Semantic search (Track A), LAN sync + browser companion (Track D) |

> Phases are sequential by default, but §3 batches and Track K items can be reordered freely by need — every batch ships independently. Phase 6 items are individually small and can be cherry-picked directly from §4.

---

## 7. Suggested immediate fixes (prioritized)

| Priority | Item | Where |
|----------|------|-------|
| **P0** | Connect tray `settingsRequested` / `clearRequested` | `ApplicationContext.cpp:224-228` |
| **P0** | Fix `fetchAll` keyset cursor (`useCount`) — silent 500-row truncation on `MostUsed` | `StorageManager.cpp:264` |
| **P0** | Reset `QSqlDatabase` before `removeDatabase` in the vacuum worker; per-run connection name | `VacuumWorker.cpp:31-34` |
| **P0** | Export/import round-trip parity (OCR, tags, snippets, saved searches; clear-all on Overwrite) | `ExportImportManager.cpp` |
| **P0** | Restore stale-lock recovery in the single-instance guard | `SingleInstanceGuard.cpp:21` |
| **P1** | JS transform execution timeout (matches the documented 2 s budget) | `ScriptActionManager.cpp:178` |
| **P1** | CSPRNG key generation + encryption error surfacing | `EncryptionManager.cpp:120` |
| **P1** | KWin script unload/cleanup lifecycle | `KWinCursorTracker.cpp:114-131` |
| **P1** | wlr-data-control: capture-type gating, offer destruction, off-thread reads | `WlrDataControlHelper.cpp` |
| **P1** | Add missing indexes (`use_count`, `entry_groups(group_id)`, pinned/sensitive) — additive migration | `DatabaseSchema.cpp` |
| **P1** | Dedup timestamp policy + payload refresh on touch | `StorageManager.cpp:39-126` |
| **P2** | Single `Ctrl+K` registration; implement or remove `>pin`/`>copy`; TimelineStrip hover | `MainWindow.cpp`, `CommandPalette.cpp`, `TimelineStrip.cpp` |
| **P2** | Accessibility names for delegate/list/popup/dialogs + Settings storage link fix | `src/app/ui/*` |
| **P2** | LICENSE, CI workflow, `docs/build.md` deps, `agent.md` module map | repo root, `docs/` |

---

## 8. Architecture notes (staying clean)

* New modules respect the existing boundary: `SearchEngine` and `TransformEngine` already live in `egoboard_core` (headless-testable; a separate `egoboard_transform` lib remains an option if the surface grows). OCR, layer-shell, wlr-data-control, KWin scripting and any future networking stay in `src/app`.
* Migrations are additive and forward-only: index-only changes, FTS5 table + triggers, a nullable `entry_embeddings` table when semantic search lands, plus a settings `ConfigVersion`. No breaking change to `history.db`, never drop user data.
* All heavy work stays off the GUI thread: `QThreadPool` for OCR, `QtConcurrent` for embeddings, the `VacuumWorker` pattern for compaction. Fix the known offenders (wlr-data-control payload reads, export/import batching) to this standard.
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

*Last updated: 2026-09-17 (full-repository review + roadmap merge) · Maintainer: local development · Next review: after the Phase 6 hardening batch — remaining long-term: semantic search (Track A), browser/LAN sync (Track D), Track H/I/J/K/L items above.*
