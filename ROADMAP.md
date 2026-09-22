# Egoboard — UI/UX Roadmap: Modern & Responsive

> Local-first clipboard history for KDE Plasma. This roadmap is UI/UX-only: make the existing Qt Widgets + KF6 app feel modern and responsive at any window size, on X11 and Wayland, without a rewrite, without cloud, without breaking the `src/core` (QtCore+Sql only) boundary.

**Status:** `v0.1.0` · Qt 6 + KF6 Widgets · local-only · MIT
**Revision 2026-09-22 (U19):** Delivered U19 (P0 Settings crash diagnosis and hardening): the deferred `QtConcurrent` workers no longer touch GUI-owned storage/settings off-thread — DB values are snapshotted on the GUI thread, workers run only external probes, app suggestions load synchronously off the indexed scan, and `tst_uidesign` gains 3 open/close regression slots (24/24 pass, full suite 29/29 + `--smoke` OK). Previous revision note preserved below.
**Revision 2026-09-22:** Full-repo re-read (`src/core`, `src/app`, `src/app/ui` × 15 widgets, `tests/` × 29, `docs/`, packaging). Prior roadmap (Phases 1–9, Tracks A–M) archived in git history (`git log -- ROADMAP.md`) — its delivered work (Search 2.0, backups/restore, palette commands, KRunner actions, tray clicks/wheel, Track M design tokens) remains the baseline. This revision adds the requested bulk image export, tray UI refresh, Settings crash diagnosis, and crash-report collection/reader toolkit. What follows remains the single forward plan, focused on modern responsive UI/UX and the new stability/data-portability work.

**Guiding principles (unchanged):**
1. Local-first, private by default. No telemetry, no network.
2. Performance is a feature — instant at 50k+ entries (`setUniformItemSizes`, `Batched`, keyset paging stay).
3. No core pollution — `egoboard_core` stays GUI-free; all UI work lives in `src/app/ui` + `SettingsManager` flags.
4. Wayland parity — notification/portal over injection.
5. QPalette over stylesheets; theme-safe on every installed scheme (contrast floors in `TextAppearance`).
6. Accessible by default — keyboard-first, screen-reader names, `Reduce motion` respected.

---

## 0. Done — shipped baseline (short log)

> Full history in `git log -- ROADMAP.md`. One-line summary, no details:
- History core: SQLite+WAL, dedup, keyset paging, FTS5 + field filters + regex/OR/NOT, highlight, recent searches + scope, saved searches, tags, sort modes.
- Capture: text/HTML/image/files, per-app ignore, type filters, debounce, pause + lock awareness, X11/Wayland trackers, wlr-data-control, layer-shell popup.
- Organize & paste: pins, nested groups + drag&drop, timeline strip, `Meta+V` / `Meta+Shift+V` / `Meta+Shift+D`, `Ctrl+1–9`, paste-as menu, close/bump/plain-text options.
- Enrich & integrate: OCR, code/link/color previews, transforms + JS sandbox, snippets + global hotkeys, KRunner `eb` + actions, palette `>commands`, tray clicks + wheel-cycle, D-Bus API.
- Privacy & durability: sensitive detect/mark/redact/exclude, SQLCipher+KWallet, expire rules, JSON/Markdown/CSV/HTML export, import/merge, backups + restore, Klipper import, integrity check.
- Shell & theming: Plasma scheme/icon follow, density, timestamps, text overrides, `DesignTokens` + `UiHelpers`, rounded popups, capped motion + reduce-motion, geometry memory, 29 test files + `--smoke`/`--bench`.

---

## 1. UI audit — where we are (2026-09-22)

### 1.1 What's already solid (do not regress)

- `DesignTokens.h`: single spacing/radius/icon/alpha/motion/row/timeline source; `UiHelpers`: hint labels, `humanSize`, `styleSearchField`, `fadeIn` + `reduceMotion` flag, contrast-checked rich-text colors.
- `MainWindow`: virtualized two-pane (`QSplitter` 3:2, `ScrollPerPixel`, `Batched/50`), debounced search (200 ms), typed field filters with hint, scope + recent-search actions, empty-state hint, geometry/splitter memory.
- `EntryDelegate`: custom paint (icon, preview, meta, pin/shield/group dots), search-term highlight, hover wash, badge tooltips, density-aware `sizeHint`.
- `PreviewPane`: stacked text/HTML/image/files + transform bar + meta footer; code highlighting derived from scheme.
- `QuickPasteMenu` / `CommandPalette`: frameless rounded shadowed cards, 120 ms fade, `Ctrl+K` once, `>commands` with completion + recents.
- `TimelineStrip` (14-day), `GroupsDock` (drag & drop), `SettingsDialog` (9-page icon sidebar, live theme preview), `TrayController` (configurable clicks + wheel-cycle).

### 1.2 Responsiveness gaps and delivery status

| # | Gap | File / symptom |
|---|-----|----------------|
| ✅ G1 | Breakpoints and adaptive splitter/drawer modes delivered in U1 on 2026-09-20; keep the geometry tests to prevent regression | `MainWindow.cpp`, `SettingsManager.cpp` |
| ✅ G2 | Wrapping/collapsible filter bar delivered in U2 on 2026-09-20; keep the narrow-width geometry coverage | `MainWindow.cpp` |
| ✅ G3 | Toolbar overflow delivered in U3 on 2026-09-20; primary actions remain visible below Wide | `MainWindow.cpp` |
| ✅ G4 | Preview drawer/single-pane behavior delivered in U1 on 2026-09-20 | `MainWindow.cpp`, `PreviewPane.cpp` |
| ✅ G5 | Quick-paste search, two-line rows and per-screen placement memory delivered in U8 on 2026-09-20 | `QuickPasteMenu.cpp`, `SettingsManager.cpp` |
| G6 | Settings `resize(800,640)`, Snippet `resize(780,480)` — fixed, sidebar + pages don't collapse on small screens | `SettingsDialog.cpp:117`, `SnippetDialog.cpp:29` |
| ✅ G7 | Removable active-filter chips delivered in U6 on 2026-09-20 | `MainWindow.cpp`, `UiHelpers.cpp` |
| ◐ G8 | Bulk-action bar and basic reinsert-based undo toast are delivered; soft-delete/trash storage and undo for every expiry path remain open | `MainWindow.cpp`, `UiHelpers.cpp` |
| ◐ G9 | List/preview skeletons and shimmer are delivered; progress dialogs exist but import/export paths are still non-cancelable and not fully progress-aware | `MainWindow.cpp`, `PreviewPane.cpp`, `SettingsDialog.cpp` |
| ◐ G10 | Touch-target tokens and font-relative timeline sizing are delivered; fractional-scale and full touch/HiDPI validation remain open | `DesignTokens.h`, `TimelineStrip.h` |
| G11 | First-run tour, settings search, per-page reset — still open (§3.7) | `SettingsDialog` |
| G12 | There is no image-only bulk export: JSON embeds image blobs as base64, while the file export path has no folder/manifest workflow and `fetchAllFull()` retains every payload in memory | `ExportImportManager.cpp:180-219,517-561` |
| G13 | Tray mode is persisted but not enforced by `TrayController`; the menu is rebuilt as eight plain-text recent actions with a static tooltip/icon and no image/type/status treatment | `SettingsManager.cpp:717-730`, `TrayController.cpp:31-170` |
| ✅ G14 | Opening Settings deferred workers calling GUI-owned storage/QSqlDatabase methods (`sourceApps()` / `stats()`) from `QtConcurrent` threads — fixed in U19 (snapshots on the GUI thread, workers probe-only, sync suggestions); open/close regression covered in `tst_uidesign` | `MainWindow.cpp:1288-1293`, `SettingsDialog.cpp` (constructor + `refreshDiagnostics`), `tests/tst_uidesign.cpp` |
| G15 | Diagnostics is a live text panel only: there is no structured crash bundle, coredump/backtrace reader, symbol/build metadata or privacy review step for sharing a failure report | `SettingsDialog.cpp:1557-1830`, `src/main.cpp`, `docs/build.md` |

---

## 2. Responsive shell (layout system)

**U1 — Breakpoints + adaptive modes ✅ delivered 2026-09-20** (`MainWindow`, `PreviewPane`, `GroupsDock`)
- Define 3 modes in `DesignTokens.h`: `Wide ≥1100px` (list + preview side-by-side), `Medium 720–1099px` (list full-width, preview collapsible bottom drawer / `QDockWidget`), `Narrow <720px` (single pane; preview opens as dialog/drawer, groups as overlay).
- Implement as `MainWindow::applyResponsiveMode(width)`: driven by `resizeEvent`, not timers. Moves `PreviewPane` between splitter and drawer without recreating it; persists per-mode splitter state in `SettingsManager` (extend `splitterState()` → per-mode keys, forward-only migration).
- `PreviewPane` minimum width becomes mode-aware (260 wide / 0 drawer); timeline hides under 560 px width behind a toggle (setting persists).
- Acceptance: resize 1366 → 800 → 600 live, no clipped controls, no horizontal scrollbar; `QT_QPA_PLATFORM=offscreen` metrics test pins mode thresholds.

**U2 — Filter bar that wraps ✅ delivered 2026-09-20** (`MainWindow::buildUi`)
- Replace single `QHBoxLayout` with search row (search + scope + recents + palette button) + collapsible "Filters" row (type/date/app/tag/sort/saved). Under `Medium`, combos collapse into one "Filters (n)" `QToolButton` menu showing active count; search keeps stretch.
- Combos get `QSizePolicy::MinimumExpanding` + `setMinimumContentsLength`, app combo keeps 140 px minimum only in `Wide`.
- Acceptance: at 700 px every control reachable, zero overlap.

**U3 — Toolbar overflow ✅ delivered 2026-09-20** (`MainWindow`)
- `m_toolbar->setToolButtonStyle` stays user-set; add overflow: Paste/Copy/Pin/Delete + Palette stay visible; Pinned-only/Audit/Delete-listed/Groups/Snippets/Chain/Settings/Clear move to a "⋯ More" menu under `Medium`. `QToolBar::setFloatable(false)`, `setMovable(false)` unchanged.
- Acceptance: no toolbar wrapping/clipping at 600 px.

---

## 3. Design system 2.0 (tokens → components)

**U4 — Token coverage ✅ delivered 2026-09-20** (`DesignTokens.h`, `UiHelpers`)
- Add: touch target minimum (44 px accessibility / 32 px compact), focus-ring width + color (contrast-checked `Highlight`), elevation (shadow blur/offset per card level), skeleton base color, toast metrics, chip metrics, drawer width fractions.
- Add `UiHelpers::makeChip()`, `makeSegmentedBar()`, `makeEmptyState(icon,title,subtitle,action)`, `makeToast()` — all palette/font-relative, `Reduce motion`-aware. Replace remaining ad-hoc stylesheets in `QuickPasteMenu`, `CommandPalette`, `GroupsDock` with helpers.
- Dark/light audit: every new color goes through `TextAppearance::ensureContrast` (≥4.5:1 text, ≥3:1 dim) — extend `tst_uidesign` contrast loop to new components.

**U5 — Density + type scale ✅ delivered 2026-09-20**
- Keep 3 densities; make them affect hit targets, not just `rowPadding` (compact ≥32 px rows, comfortable ≥40, spacious ≥48). Tie `TimelineStrip::sizeHint` height to font (`48 → max(44, font*2.6)`).
- Text-size delta (`-2…+6pt`) must scale chips, toasts, palette rows, timeline captions — no fixed `11px` anywhere (already removed once; keep the test that forbids it).

---

## 4. Core surfaces

**U6 — History list ✅ delivered 2026-09-20** (`EntryDelegate`, `ClipboardListModel`, `MainWindow`)
- Sticky group-by-day headers as optional mode (setting; default off) — delegate draws header rows, model exposes section roles; virtualization unaffected.
- Row extras behind settings (all default off): entry index, use-count badge, "pasted today" dot.
- Active-filter chips row (finishes Track K chip item): one chip per active filter (type/date/app/tag/pinned/sensitive/day/field-query), × removes it; replaces read-only `m_queryHint` with chips + hint line.
- Bulk-action bar: appears when `ExtendedSelection >1`: Pin / Tag / Group / Export / Delete + count + Clear; wired to existing storage batch paths in one transaction.
- Single-click paste stays opt-in (default double-click/Enter).

**U7 — Preview pane ✅ delivered 2026-09-20** (`PreviewPane`)
- Drawer/dialog mode (from U1); header gains: copy, pin, open-source-app label, close (drawer only).
- Image zoom & pan (wheel zoom, fit/100 % toggle); text preview gets line-wrap toggle + copy-selection button.
- Inline edit (behind setting): edit → save re-hashes/re-indexes via existing `insertOrUpdate` path; dirty state blocks selection change without confirm.
- Color & QR details: big swatch + RGB/HSL copy buttons; QR-encode URLs with local lib only (new optional dep, off if absent).
- Screenshare/privacy blur: blur payload while screencast active or `sensitiveMode==Mark` until hover/focus (setting).

**U8 — Quick paste 2.0 ✅ delivered 2026-09-20** (`QuickPasteMenu`) — closes the Phase 5 leftover
- Search-as-you-type `QLineEdit` on top (reuses `styleSearchField` + `SearchEngine` field-filter parser lite: plain text + `type:`/`app:`), two-line rows (preview + meta) behind setting, multi-monitor placement memory (per-screen `QPoint` in settings).
- Keyboard: ↑/↓ + 1–9 + Enter + Esc unchanged; focus starts in search; `Ctrl+K` hint parity with main window.
- Acceptance: 9-item list filters live at 50k DB without blocking (reuse `fetchPage` limit path).

**U9 — Command palette ✅ delivered 2026-09-20** (`CommandPalette`, `PaletteCommands`)
- Result rows show type icon + source app + relative time (same delegate language as list, compact).
- Argument completion shows inline ghost text (Tab/Enter semantics unchanged); `>export` preselects format (already) + `>tag`/`>group` create-if-missing inline.
- Recent-history section when input empty (from `recentSearches` + `recentPaletteCommands`).

**U10 — Timeline + Groups ◐ partial** (`TimelineStrip`, `GroupsDock`)
- Timeline: keyboard-navigable bars (←/→ + Enter), accessible names per bar ("12 entries, Monday"), collapses to a combo under `Narrow`.
- Groups: overlay drawer mode under `Medium`; drop-target highlight already done — add count badge on drag (reuse quick-paste badge language); empty state with "New group" action.

**U17 — Bulk image export (P1)** (`ExportImportManager`, `ExportImportDialogs`, bulk-action bar)
- Add an image-only export flow for the current selection/filter, all entries, pinned entries, and a group subtree. It writes the stored PNG blobs to a user-selected directory without changing history; entries with no stored blob are reported as skipped rather than producing empty files.
- Use deterministic, collision-safe filenames (timestamp + entry id/hash) and write a small manifest containing the source app/window, capture time, pinned/sensitive flags, tags, OCR text and the generated filename. The manifest must not expose payload text unless the user explicitly chooses a metadata format that includes it.
- Stream bounded pages instead of calling `fetchAllFull()` for the whole database. The dialog shows count/bytes/progress, supports cancel, reports write failures, and makes the sensitive-entry policy explicit before export. Existing JSON/Markdown/CSV/HTML export behavior stays unchanged.
- Acceptance: multi-image export at 50k entries stays within the export budget and bounded memory; duplicate timestamps/names never overwrite; cancellation leaves no misleading manifest; read/write errors are actionable; offscreen tests cover selection/filter scopes, skipped blobs, sensitive entries and filename collisions.

---

## 5. Feedback, motion, states

**U11 — Toasts + undo ◐ partial** (`UiHelpers::makeToast`, `MainWindow`)
- Non-modal bottom toast for: delete entry, bulk delete, clear history, expire sweep, copy/paste confirm — with Undo (5 s soft-delete window via existing storage trash path; new `trash` table, additive migration, auto-purge).
- Import/export/restore/backup: real `QProgressDialog` (cancelable) replacing wait-cursor-only; worker-thread paths already exist (`BackupService`, import transaction) — hook progress signals.
- Skeleton rows in list + shimmer in preview while `fetchPage`/OCR pending; `Reduce motion` turns shimmer into static placeholder.

**U12 — Motion language ◐ partial** (extends Track M M4)
- Keep 120 ms cap; add: drawer slide (80 ms), chip fade/scale (80 ms), toast slide-up (120 ms) — all skipped under `Reduce motion`. One `UiHelpers::animate()` entry; no per-widget durations.

**U13 — Empty / error / offline states ◐ partial**
- One `makeEmptyState` everywhere: no-entries-yet (with shortcut hints), no-match (with "Clear filters"), group-empty, snippet-empty (already hinted — unify), OCR-missing (with install hint), DB-error (with integrity-check action). Each has icon + title + one action, never bare text.

**U16 — Capture feedback: sound + notification, top-aware ✅ delivered 2026-09-22**
- Goal: play a sound + show a notification on every *new* copy; stay silent when the copy is already at the top; re-copying an older entry moves it back to the top.
- Move-to-top is already the storage contract (`insertOrUpdate` dedup touch: `timestamp = MAX(...)`, `use_count + 1`, `entryTouched` → model moves row to 0 in trivial Newest view). This item keeps that contract and pins it with a regression test — no schema change.
- Top check in `ApplicationContext::onCaptured` (single funnel for both `ClipboardWatcher` and `WlrDataControlHelper` paths): before `insertOrUpdate`, read the newest row (`fetchPage({}, {}, 1)`) and compare `content_hash`. If equal → already-at-top: skip sound (and notification), still allow the touch (`use_count` bump, row stays at 0). Covers empty history (no top → feedback) and pinned rows (order is timestamp-based, pins ignored).
- Feedback, each behind its own setting (both default on, both `tr()`, both gated by global `notificationsEnabled` for the popup): `captureSoundEnabled` (`[Ui] CaptureSound`) → `QApplication::beep()` (zero new deps; themed sound via `egoboard.notifyrc` later, not in this item); `captureNotificationEnabled` (`[Ui] CaptureNotification`) → `KNotification::event("capture", ...)` with preview + source app, `CloseOnTimeout`, suppressed for own paste-back writes (never reach `onCaptured`), ignored apps, excluded-sensitive and paused capture.
- Settings UI: two checkboxes under General ▸ Tray & notifications ("Play a sound on new copy", "Show a notification on new copy") + diagnostics line; live via existing `SettingsManager::changed()`.
- Tests (offscreen): settings persist + defaults; `insertOrUpdate` twice with same hash bumps `use_count`/timestamp and keeps row 0; top-hash-equal → no-feedback decision (pure helper, no `KNotification` in tests); full suite + `--smoke` green. Boundary: no `QtWidgets`/`KF6` in `src/core`, no DB work off the GUI thread beyond existing paths.

---

## 6. Settings, onboarding, help

**U14 — Settings dialog** (`SettingsDialog`)
- Search box atop dialog filtering pages + highlighting matching knobs (open §3.7 item); per-page "Reset to defaults"; settings export/import JSON next to history backup.
- Responsive: sidebar → top tabs under 640 px width; pages scroll (`QScrollArea` already partial — finish); Storage page shows DB path + quota bar + integrity actions in one card.
- Profiles ("Work"/"Personal"): switch whole setting sets from palette (`>profile`); stored as named `KConfig` groups.

**U19 — Settings crash diagnosis and hardening (P0)** ✅ *delivered 2026-09-22* (`MainWindow::openSettings`, `SettingsDialog`, `tst_uidesign`)
- All entry points (window action, tray `settingsRequested`, palette `settingsRequested`) funnel into `MainWindow::openSettings()`; the crash surface was the dialog's deferred `QtConcurrent` workers touching the GUI-owned `StorageManager`/`SettingsManager` off-thread (`sourceApps()` in the constructor, `stats()`/`ocrLanguage()`/`databasePath()` in `refreshDiagnostics`).
- Fix (root cause, non-blocking open preserved): DB/settings values are snapshotted on the owning thread before dispatch; workers run only the external `tesseract`/`kwin` probes and carry snapshots by value; app suggestions load synchronously in the deferred slot off the indexed `idx_entries_app` scan; delivery stays lifetime-safe (`QPointer` guard + queued `invokeMethod` on `qApp`).
- Regression in `tst_uidesign` (same `QPointer` + queued-handoff shape against the real `StorageManager`, populated and empty DBs): snapshots never touch storage off-thread, early-close delivery no-ops, 20× open/close stress + live handoff — 24/24 pass, full suite 29/29 + `--smoke` OK.

**U20 — Crash report collection and reader toolkit (P1)** (`src/main.cpp`, `SettingsDialog`, diagnostics tooling)
- Add a local-first support workflow exposed both from Settings ▸ Diagnostics and the CLI: create a bounded, structured report containing Egoboard version/build ID, debug-symbol availability, Qt/KF6 versions, QPA/session, X11/Wayland/KWin details, recent sanitized application logs, the failing signal/thread and a symbolized backtrace when one is available. Provide an `Open/Read crash report` path that renders the same schema for a user or developer.
- Integrate with standard tools when present: `coredumpctl`/systemd-coredump for locating and extracting the latest Egoboard dump, `journalctl --user` for scoped logs, and `gdb`/`addr2line` (or an equivalent installed symbolizer) for stack resolution. Detect missing tools and report a useful manual command/fallback instead of failing or requiring root. Never invent a parser for an unavailable dump format.
- Ship a `RelWithDebInfo`/symbol guidance path and include executable/build-id information so a report can explain when a backtrace is unsymbolized. The reader must accept partial or malformed reports, highlight the first Egoboard frame, and keep raw tool output available for debugging.
- Privacy is opt-in and visible before saving: never include clipboard text, image blobs, history.db, SQLCipher/KWallet material, full environment secrets or unredacted home paths. Redact usernames/paths where practical, show the final bundle contents, and make network upload explicitly out of scope.
- Acceptance: reports can be created and read with and without systemd-coredump/gdb installed; malformed/partial fixtures render safely; size and log windows are capped; an intentional test fixture verifies signal/backtrace parsing without crashing the production app; offscreen tests cover schema, redaction and CLI/Settings parity.

**U15 — First-run + discoverability**
- 4-step overlay tour (hotkeys, palette, privacy, settings search) on first launch only; skippable, never re-shows without asking.
- Shortcut cheatsheet (`?` in main window + palette footer); tray tooltip shows pause state + last capture time.

---

## 7. Accessibility & input (non-negotiable)

- Full keyboard map preserved + new: `Alt+1…9` focus areas (search/list/preview/groups/timeline), `Esc` clears chips before closing, `/` focuses search.
- Every interactive surface keeps `AccessibleName/Description`; chips, toasts, drawer close, timeline bars all named. High-contrast delegate path tested with forced `HighContrast` palette in `tst_uidesign`.
- Touch: ≥32 px targets at compact, timeline bars + swipe on list (`QScroller` touch-drag already via `ScrollPerPixel` — enable `QScroller::grabGesture` for touchscreens).
- i18n: all new strings `tr()`; layout-tolerant (German-length labels must not clip — test with `de` pseudo-long strings offscreen).

---

## 8. Platform & integration UI

- **Wayland portal paste**: opt-in toggle with per-session consent explainer UI (what it does, how to revoke); fallback notification path stays.
- **Screencast awareness**: status-bar/dot indicator + auto-blur (U7); best-effort via compositor signals.
- **KRunner**: live preview pane + result categories (open Track J tail).
- **Tray**: tooltip + menu refresh and paused-state icon overlay are tracked in U18 (theme-safe, SNI/fallback parity).

**U18 — Tray UI refresh (P1)** (`TrayController`, `SettingsManager`, tray tests)
- Make `TrayMode` live and authoritative: `auto` follows the history/visibility policy, `always` keeps the icon available, and `hidden` removes the tray surface while leaving hotkeys and the process running. React to `SettingsManager::changed()` without requiring a restart.
- Refresh the menu into clear action groups: current capture state and history count, quick paste/show history, pause/resume, recent entries, Settings, clear history and Quit. Recent rows remain bounded and lazy, but gain type-aware icons/labels, image-friendly previews and explicit empty/loading states. Keep configured primary/secondary click behavior, wheel cycling, and `KStatusNotifierItem`/`QSystemTrayIcon` fallback semantics intact.
- Update tooltip and icon state when paused/resumed or a new capture arrives; use only `QIcon::fromTheme()` and palette/theme-safe state treatment, including the paused overlay. Do not add bundled tray artwork.
- Acceptance: SNI and fallback expose the same actions and labels; changing tray mode/click actions is visible immediately; no menu rebuild signal storm or stale pause state; tests cover menu actions, mode transitions, empty/recent/image entries and pause/capture tooltip updates.

---

## 9. Quality gates (every UI batch)

- `cmake --build build && ctest --test-dir build --output-on-failure && QT_QPA_PLATFORM=offscreen ./build/egoboard --smoke`
- New/changed widgets get offscreen tests (`tst_ui` / `tst_uidesign` pattern): geometry at 600/800/1366 px, contrast over installed schemes, paint/hit-test agreement, no fixed-pixel regressions.
- `--bench` budgets must not regress (list paging, FTS, export).
- Boundary check: no `QtWidgets`/`KF6` in `src/core`; feature flags in `SettingsManager` with `changed()` → live UI reaction → diagnostics entry.

---

## 10. Phased delivery

| Phase | Focus | Ships |
|-------|-------|-------|
| **R0 — Foundations** ✅ *landed 2026-09-20* | Tokens + helpers + tests | U4, U5: touch targets (24/32/44), focus ring, elevation tokens, skeleton/toast/chip/drawer metrics, breakpoints (720/1100), `makeChip/makeEmptyState/makeToast/animate/cardShadow/styleItemList`, shared popup cards, font-relative timeline + search field, 6 new `tst_uidesign` cases (18/18 pass), full suite 29/29 + `--smoke` OK |
| **R1 — Responsive shell** ✅ *landed 2026-09-20* | Breakpoints, wrapping filter bar, toolbar overflow | U1 (Wide/Medium/Narrow via `resizeEvent`, preview splitter↔drawer reparent, timeline collapse <560px, per-mode splitter keys), U2 (search row + collapsible filter row, `Filters (n)` live-mirror menu, expanding combos), U3 (Paste/Copy/Pin/Delete + Palette stay, 6 actions → `More` menu off Wide); `tst_uidesign` 19/19, full suite 29/29 + `--smoke` OK |
| **R2 — List & preview** ✅ *landed 2026-09-20* | Chips, bulk bar, day headers, drawer preview, zoom/edit | U6 (removable filter chips + `Filters (n)` count, bulk bar Pin/Unpin/Tag/Group/Export/Delete, entry-index + use-count row extras, day-header helper), U7 (preview header Copy/Pin/source/Close, image zoom slider + Fit/100%, wrap toggle, inline edit, sensitive blur overlay), undo toast on delete/bulk/clear (re-insert restore); `tst_uidesign` 21/21, full suite 29/29 + `--smoke` OK |
| **R3 — Popups 2.0** ✅ *landed 2026-09-20* | Quick-paste search + two-line rows + monitor memory; palette rows + recents | U8 (search-as-you-type over bounded 200 recents + `type:`/`app:` filters, two-line preview+meta rows behind setting, per-screen placement memory, ↑↓/Enter/Esc in search), U9 (rich two-line delegate with type icon + app + age, empty-input recents section, Tab ghost hint, window feeds recents); `tst_paletteui` 15/15, full suite 29/29 + `--smoke` OK |
| **R4 — Feedback & states ◐ partial** | Skeletons, empty-state/toast/motion foundations and capture sound+notification delivered; undo/progress polish and bulk image export remain | U11, U12, U13, **U16** ✅; U17 |
| **R5 — Settings & onboarding** ◐ *partial (U19 landed 2026-09-22)* | Settings search + reset + profiles + responsive dialog; first-run tour; cheatsheet; Settings crash hardening ✅ and crash-report toolkit | U14, U15, **U19** ✅, **U20** |
| **R6 — Platform polish** | Portal-paste consent UI, screencast blur, KRunner preview, tray UI refresh | **U18**, §8 |
| **Later** | Semantic search UI, LAN-sync pairing UI, browser companion, stats dashboard, CopyQ `.cpq` reader, `.zip` backups | Carried long-term; each needs its own UI pass against this system |

> R0–R2 are sequential (shell before surfaces). R3–R6 can reorder by need — each ships independently. **U19 (P0) is delivered**; next are U17/U18/U20 (P1), which can proceed independently.

---

## 11. Non-goals

- No QML/Electron rewrite — Qt Widgets + KF6 stays.
- No cloud, no account, no telemetry, no AI exfiltration.
- No bundled themes/icons — runtime-discovered, read-only.
- No vacuum/DB work on the GUI thread; no hardcoded paths (use `QStandardPaths` + `SettingsManager`).

---

*Last updated: 2026-09-22 (status synced: U1–U9, U16 and U19 delivered; U11–U13 partial; U17/U18/U20 open) · Next: U17 (P1 bulk image export), U18 (P1 tray refresh), U20 (P1 crash-report toolkit) — independent.*
