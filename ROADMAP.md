# Egoboard — UI/UX Roadmap: Modern & Responsive

> Local-first clipboard history for KDE Plasma. This roadmap is UI/UX-only: make the existing Qt Widgets + KF6 app feel modern and responsive at any window size, on X11 and Wayland, without a rewrite, without cloud, without breaking the `src/core` (QtCore+Sql only) boundary.

**Status:** `v0.2` · Qt 6 + KF6 Widgets · local-only · MIT
**Revision 2026-09-24 (housekeeping + P2 scoping):** Compressed delivered history into the ledger below (detail lives in `git log -- ROADMAP.md`); per-item progress paragraphs folded into their headers. Decided P2 horizon from 6 answers: Conservative scope (dashboard + `.zip` backups + CopyQ `.cpq` only), optional system deps, local-trust privacy, full platform matrix, minimal docs now, dashboard-first sequence. No code changed; nothing committed.

## Delivery ledger (one line each; detail in git history)

> Full history in `git log -- ROADMAP.md`. Format: ID — date — what shipped — test evidence.

- P1 docs — 2026-09-23 — README to actual product + `docs/build.md` PipeWire dep + `docs/release-checklist.md` + `tst_packaging` + v1-export back-compat slot — ctest 34/34 + `--smoke` OK.
- R6 screencast — 2026-09-23 — `ScreencastWatcher` PipeWire probe + status indicator + preview auto-blur via `UiHelpers::shouldBlurPreview()` — `tst_screencast` + 1 `tst_uidesign` slot 5/5, ctest 33/33 + `--smoke` OK.
- R6 portal paste — 2026-09-23 — `PortalPaster` RemoteDesktop Ctrl+V behind `portalPasteEnabled` + Settings opt-in + availability status — 3 `tst_autopaster` + 1 `tst_settings` slots, ctest 32/32 + `--smoke` OK.
- R6 KRunner categories — 2026-09-23 — `EntryRow::matchCategory()` + `setMatchCategory()` with translated labels + relevance — 1 `tst_krunner` slot 18/18, ctest 32/32 + `--smoke` OK.
- §7 touch + pseudo-long — 2026-09-23 — `enableTouchScroll()` + `pseudoLong()` probe, tour clip-free at default + 360 px — 3 `tst_uidesign` slots, ctest 32/32 + `--smoke` OK.
- U10 groups + narrow — 2026-09-23 — `GroupsDock::setOverlayMode()` drawer + `GroupTreeModel::entryCount()` badge + `TimelineStrip` narrow combo — 1 `tst_grouptreemodel` + 2 `tst_uidesign` slots, ctest 32/32 + `--smoke` OK.
- U15 tour — 2026-09-23 — `FirstRunTour` 4-step once-only dialog + `tourSeen` flag + `>tour` + toolbar action — `tst_tour` 5/5 + `tst_settings`/`tst_palette` slots, ctest 32/32 + `--smoke` OK.
- U14 profiles — 2026-09-23 — named profiles as `KConfig` groups + `>profile` + Storage Profiles row — 3 `tst_settings` + 1 `tst_palette` slots (41/41 + 10/10), ctest 31/31 + `--smoke` OK.
- U14 per-page reset — 2026-09-23 — `resetPageToDefaults()` + 7 page buttons — 4 `tst_settings` slots 38/38, ctest 31/31 + `--smoke` OK.
- U15 cheatsheet + §7 keys — 2026-09-23 — `ShortcutCheatsheet` + `/`, `Alt+1…5`, `Esc` unwind — `tst_cheatsheet`, ctest 31/31 + `--smoke` OK.
- U10 timeline — 2026-09-23 — keyboard bars + per-bar accessible names via proxy-identity AT children + shared `barRect()` — 2 `tst_uidesign` slots, 30/30 + `--smoke` OK.
- U14 responsive — 2026-09-23 — sidebar to top strip under 640 px + all pages scroll — 2 `tst_uidesign` slots, 30/30 + `--smoke` OK.
- U14 search — 2026-09-23 — dialog search box + harvested knob texts + bold matches — 3 `tst_uidesign` slots, 30/30 + `--smoke` OK.
- U14 portability — 2026-09-23 — `exportToJson`/`importFromJson` v1 envelope + Storage buttons — 6 `tst_settings` slots 30/30, ctest 30/30 + `--smoke` OK.
- U11 — 2026-09-23 — trash-based undo + cooperative cancel/progress across export/import/backup/restore — 8 + 8 `tst_exportimport`/`tst_storage` slots, 30/30 + `--smoke` OK + `--bench=100000` in budgets.
- U13 — 2026-09-23 — unified `makeEmptyState` + `clearAllFilters()` + `ocrMetaSuffix()` + persistent integrity error state — 3 `tst_uidesign` slots, 30/30 + `--smoke` OK.
- U12 — 2026-09-23 — true positional `UiHelpers::animate()` (fade/chip/slide/rise, token durations, reduce-motion skip) — 5 `tst_uidesign` slots, 30/30 + `--smoke` OK.
- U20 — 2026-09-22 — QtCore-only `CrashReport` + `--crash-report`/`--read-crash-report` + Diagnostics flows — 13-slot `tst_crashreport`, 30/30 + `--smoke` OK.
- U18 — 2026-09-22 — live `TrayMode` + grouped `TrayMenuModel` menu + pause-aware tooltip/icon — 6 `tst_traycycle` slots 13/13, suite 29/29 + `--smoke` OK.
- U17 — 2026-09-22 — `exportImages()` bounded streaming + manifest + dialog + `>export images` (+ JPEG encoder-hook follow-up) — 10 `tst_exportimport` slots 26/26, suite 29/29 + `--smoke` OK.
- U19 — 2026-09-22 — Settings thread-hardening (GUI-thread snapshots, probe-only workers, `QPointer` handoff) — 3 `tst_uidesign` regression slots 24/24, suite 29/29 + `--smoke` OK.
- U16 — 2026-09-22 — capture sound + notification with already-at-top suppression (`captureSoundEnabled`, `captureNotificationEnabled`) — settings + dedup + no-feedback-decision tests + `--smoke` OK.
- R0–R3 — 2026-09-20 — tokens/helpers, responsive shell (U1–U3), list/preview (U6–U7), popups 2.0 (U8–U9) — per-phase `tst_uidesign`/`tst_paletteui` gates, suites 29/29 + `--smoke` OK.
- Baselines — 2026-09-22/23 full-repo reviews archived prior phases/tracks; prior roadmap text preserved in git history.

**Still open (verbatim, manual verification):**

- Best-effort limits disclosed: absence of a recognizable node reads as inactive, and the positive case needs a manual Plasma Wayland check.
- The live portal handshake itself needs a compositor (manual Plasma Wayland check).
- compositor-dependent behavior stays manual per the release checklist (see `docs/release-checklist.md`).

---

### Forward roadmap — repository-wide priorities

**Current architecture and baseline**

- Native C++20 / Qt 6.8+ / KF6 application, with SQLite-backed `egoboard_core` separated from the desktop runtime and `ApplicationContext` as composition root. `GroupTreeModel` is the documented GUI-dependent exception compiled into the app.
- Core capabilities include WAL-backed deduplicated history, schema migrations, FTS and filtering, keyset paging, snippets/transforms, groups/bookmarks, import/export, sensitive-data rules, expiry, and background vacuuming. App integration covers clipboard capture, X11/Wayland metadata and paste paths, shortcuts, tray, D-Bus, KRunner, OCR, optional SQLCipher/KWallet, themes, settings, backups, and crash reports.
- UI includes responsive history/preview, quick paste, command palette, groups, timeline, settings, and export/import dialogs. The repository has broad QtTest coverage plus `--smoke` and `--bench`; CMake supports optional integrations and AppImage scripts. Build instructions and README are concise and do not yet describe the full shipped feature set.
- Preserve the established constraints: Qt Widgets/KF6, local-only operation, no bundled themes/icons, GUI-free core, additive data-safe migrations, parameterized SQL, Wayland-safe paste behavior, and existing theme discovery/application behavior.

**P0 — Finish the already-started user-facing work**

1. **R4 feedback and states ✅ delivered 2026-09-23:** trash-based undo for deletes, clear-history and expiry sweeps (exact-ID restore, no snapshot spikes); cancellable progress for backup/import/export/restore (cooperative cancel + progress, rollback on abort); non-blocking loading placeholders; U12 motion and U13 empty/error states. Keep `Reduce motion` and accessibility behavior throughout.
2. **R5 settings and onboarding ✅ delivered 2026-09-23:** U14 settings search, per-page reset, responsive narrow layout, storage status/quota/integrity presentation, and named profiles (JSON portability, dialog search, responsive narrow, per-page reset, profiles); U15 first-run tour, shortcut cheatsheet, and last-capture tray guidance (tour + cheatsheet + §7 keys; tray guidance covered by U18). Thread-safe diagnostics and crash-report flows retained (U19/U20).
3. **R6 platform polish ✅ delivered 2026-09-23:** opt-in Wayland portal paste consent/revocation flow (`PortalPaster` RemoteDesktop Ctrl+V behind `portalPasteEnabled`, per-session compositor consent, notification fallback always stays), best-effort screencast status/blur (`ScreencastWatcher` PipeWire probe + status-dot indicator + preview auto-blur), and KRunner live preview/result categories (content-type grouping with relevance ordering; KRunner has no preview-pane API — per-keystroke multi-line match text is the live preview). U18 tray refresh is complete; retain parity across SNI and fallback surfaces.
4. **U10 and accessibility tail ✅ done 2026-09-23:** keyboard navigation/accessible labels for timeline and groups (timeline keyboard + per-bar names; groups overlay drawer + drop count badge + narrow day combo), narrow groups overlay/empty state/count feedback, touch scrolling (`TouchGesture` on the history list viewport), and pseudo-long translation layout checks (German-length probe, tour clip-free at default + narrow widths). High-contrast and keyboard-first behavior stay acceptance requirements.

**P1 — Reliability, portability, and maintainability**

5. **Document the actual product ✅ delivered 2026-09-23:** README covers shipped capture types, search syntax, privacy/encryption options, backups/import/export (+ settings JSON, profiles), shortcuts, KRunner/D-Bus, optional dependencies, platform differences, and diagnostics; `docs/build.md` lists every optional build/runtime input with its fallback; claims verified against runtime feature detection and build flags.
6. **Platform and packaging verification ✅ delivered 2026-09-23:** `docs/release-checklist.md` repeats the gates for X11 and Plasma Wayland, the optional-feature matrix (KRunner, layer shell, XTest, SQLCipher, PipeWire) with expected absent behavior, installed desktop entry/autostart behavior, and AppImage validation; `tst_packaging` pins desktop-entry validity and helper-script presence headlessly.
7. **Data safety and recovery UX ✅ delivered 2026-09-23:** cancel/progress/integrity/failure UX shipped across U11/U13 (chunked GUI-thread runs, rollback on abort, persistent integrity state); forward-only migrations retained; v1-export back-compat import covered in `tst_exportimport`; large-data ops stay bounded off the GUI thread.
8. **Privacy controls and diagnostics ✅ verified 2026-09-23:** sensitive handling explicit across capture (skip/redact notifications on both paths), previews (blur + OCR state), notifications (gated, sensitive-suppressed), exports/backups (sensitive policy, no secrets in crash reports/diagnostics); effective settings and subsystem availability discoverable via Settings ▸ Diagnostics, portal/screencast/OCR status lines, and the sharing indicator.
9. **Integration regression coverage ✅ delivered 2026-09-23:** headless coverage for platform/fallback decisions (paste-method matrix, portal/screencast probes, layer-shell/data-control diagnostics) and packaging metadata (`tst_packaging`); compositor-dependent behavior stays manual per the release checklist.

**P2 — Long-term product extensions (standing evaluation, verbatim)**

10. Evaluate semantic search UI, LAN-sync pairing, browser companion, usage dashboard, CopyQ `.cpq` import, and `.zip` backups as separate scoped proposals. Define privacy, compatibility, dependency, migration, and performance requirements before implementation; each must fit the local-first model and receive its own UI/architecture review.

**P2 — Next horizon (decided 2026-09-24 from 6 answers)**

Scope: Conservative P2 only — usage dashboard, `.zip` backups, CopyQ `.cpq` import. Never-line: no cloud, no account, no telemetry, no AI exfiltration, no network listeners; semantic search, LAN-sync pairing, and browser companion stay out of the horizon. Deps: optional system deps allowed (`EGOBOARD_HAVE_QUAZIP`/`EGOBOARD_HAVE_QTCHARTS`-style auto-detect, graceful fallback, absence matrix in release checklist). Privacy: local-trust default — payloads stay on local disk, sensitive flags preserved but visible; only crash reports/diagnostics stay redacted. Platform: full matrix per item (headless ctest + manual X11 + Plasma Wayland + AppImage validation). Docs: minimal now (in-app hints + release checklist per item; README/`docs/build.md` once at P2 end). Sequence: P2-A dashboard → P2-B `.zip` → P2-C `.cpq`, each landing independently.

- **P2-A — Usage dashboard (first).** Read-only local stats over existing indexes: counts by day/app/type, top apps, size histogram, streak; custom `QWidget`/`QPainter` fallback when QtCharts is absent (no bundled assets; `QPalette` colors, `tr()` strings). Acceptance: opens from toolbar/Menu, respects `Reduce motion`, keyboard-navigable, AT names on charts/summaries; empty-DB empty-state. Dependencies: optional QtCharts; no schema change (reads existing indexes only). Privacy: aggregates only; payload text never rendered; sensitive counts labeled without content. Performance: 50k-entry aggregates < 500 ms offscreen bench; bounded 200-row pages, no `fetchAllFull`; no GUI-thread VACUUM. Tests: new `tst_dashboard` slots (aggregates fixture, empty state, fallback render offscreen, contrast over installed schemes) + release-checklist X11/Wayland passes.
- **P2-B — `.zip` backups (second).** Single-file backup: DB + settings JSON + manifest in one `.zip` via optional system zip dep with graceful fallback to current multi-file backup when absent. Acceptance: Settings ▸ Storage offers `.zip` when available, progress + cooperative cancel, atomic write (temp + rename), restore validates manifest before replacing, never overwrites without confirm. Dependencies: optional QuaZip/libzip-style flag; forward-only, no destructive migration. Privacy: local-trust — full local contents incl. sensitive by default with explicit label; SQLCipher state preserved; secrets stay in KWallet, never enter the file. Performance: 50k-entry backup/restore within current `--bench` budgets; chunked I/O with progress. Tests: `tst_exportimport` slots (round-trip, cancel atomicity, corrupt-zip rejection, absent-dep fallback) + X11/Wayland checklist + AppImage validation.
- **P2-C — CopyQ `.cpq` import (third).** Read-only importer for unencrypted CopyQ exports, researched against upstream source first; encrypted/compressed variants deferred with a precise roadmap note (magic/version finding) rather than a speculative parser. Acceptance: file picker + scope preview + merge via content-hash dedup in one bulk transaction with single refresh; rollback on cancel. Dependencies: none new (QtCore-only parser); scratch-DB staging so a running CopyQ instance is never locked. Privacy: imported sensitive-looking entries re-flagged by `SensitiveDataDetector`; source noted in manifest. Performance: 10k-item import < 5 s offscreen, bounded batches, no signal storm. Tests: `tst_exportimport` slots (fixture import, dedup merge, cancel rollback, unsupported-variant refusal) + checklist passes.

**Delivery order:** P0 items first (R4 → R5 → R6/U10, with independent items reorderable); then P1 documentation, release verification, recovery/privacy polish, and regression coverage; P2 only after explicit design and scope. Each change keeps Qt Widgets, current theme behavior, the GUI-free core, data-preserving migrations, and the established build/test/smoke/benchmark gates. The detailed U-item descriptions, acceptance notes, statuses, tests, and prior delivery history below remain authoritative; this summary does not replace or discard them.

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
| ◐ G6 | Settings responsive narrow layout delivered 2026-09-23 (top strip under 640 px, all pages scroll); Snippet stacking still open | `SettingsDialog.cpp`, `UiHelpers.cpp` |
| ✅ G7 | Removable active-filter chips delivered in U6 on 2026-09-20 | `MainWindow.cpp`, `UiHelpers.cpp` |
| ✅ G8 | Bulk-action bar and trash-based undo toast delivered (soft-delete storage + exact-ID restore for deletes, clear-history and every expiry path; reinsert path retired) — fixed 2026-09-23 in U11 | `MainWindow.cpp`, `StorageManager.cpp`, `ExpireScheduler.cpp` |
| ✅ G9 | List/preview skeletons and shimmer are delivered; import/export/restore/backup run with cooperative cancel + progress (U17 pattern extended to JSON/CSV/Markdown/HTML, JSON/Klipper import with rollback, worker backup/restore with dialogs) — fixed 2026-09-23 in U11 | `ExportImportManager.cpp`, `BackupService.cpp`, `MainWindow.cpp`, `SettingsDialog.cpp` |
| ◐ G10 | Touch-target tokens and font-relative timeline sizing are delivered; fractional-scale and full touch/HiDPI validation remain open | `DesignTokens.h`, `TimelineStrip.h` |
| ◐ G11 | Settings search, per-page reset, first-run tour delivered 2026-09-23 (filter + bold, 7 page-reset buttons, 4-step once-only tour); profiles in `SettingsManager` + `>profile` | `SettingsDialog`, `SettingsManager.cpp`, `FirstRunTour` |
| ✅ G12 | No image-only bulk export — fixed in U17: `exportImages()` streams bounded pages (no `fetchAllFull`), writes PNG blobs + manifest, reports skipped blobs | `ExportImportManager.cpp`, `ExportImportDialogs.cpp`, `MainWindow.cpp` |
| ✅ G13 | Tray mode persisted but not enforced; plain-text recent menu with static tooltip/icon — fixed in U18 (live mode, grouped model-driven menu, pause-aware tooltip + theme paused icon) | `TrayController.cpp`, `TrayMenuModel.h`, `tests/tst_traycycle.cpp` |
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

**U10 — Timeline + Groups ✅ delivered 2026-09-23** (`TimelineStrip`, `GroupsDock`; timeline keyboard+AT, groups overlay drawer + count badge + narrow combo; ctest 32/32 + `--smoke` OK)
- Timeline: keyboard-navigable bars (←/→ + Enter), accessible names per bar ("12 entries, Monday"), collapses to a combo under `Narrow`.
- Groups: overlay drawer mode under `Medium`; drop-target highlight already done — add count badge on drag (reuse quick-paste badge language); empty state with "New group" action.

**U17 — Bulk image export (P1)** ✅ *delivered 2026-09-22* (`ExportImportManager`, `ExportImportDialogs`, bulk-action bar; 10 `tst_exportimport` slots 26/26, suite 29/29 + `--smoke` OK; JPEG encoder-hook follow-up covered in `tst_uidesign`)
- `exportImages()` covers selection / current filter / everything / pinned / group-subtree scopes: bounded 200-row keyset streaming plus single parameterized IN-query payload batches (never `fetchAllFull`), deterministic `egoboard-<timestamp>-<id>-<hash8>.png` names with `-n` collision suffixes and `QIODevice::NewOnly` so existing files are never overwritten, and a `manifest.json` (source app/window, time, pinned/sensitive, tags, OCR; payload text only with explicit opt-in).
- Explicit sensitive policy (skip-and-report by default), per-batch progress callback + atomic cancel (chunked synchronous run on the GUI thread — no worker-thread SQL), cancel/write-failure writes no manifest and reports how far the run got; history is never modified and JSON/Markdown/CSV/HTML exports are unchanged.
- UI: bulk-bar Export opens an `ImageExportDialog` (scope radios, folder picker, sensitive/text checkboxes, privacy hint) with cancelable progress and a result breakdown; `>export images` (palette completion + usage) routes to the same flow.
- Scale probe: 5000 images × 20 KB (100 MB) in ~0.6 s (~8500 img/s).

---

## 5. Feedback, motion, states

**U11 — Toasts + undo ✅ delivered 2026-09-23** (`UiHelpers::makeToast`, `MainWindow`; trash undo + cancelable progress; 16 `tst_storage`/`tst_exportimport` slots, `--bench=100000` in budgets)
- Non-modal bottom toast for: delete entry, bulk delete, clear history, expire sweep, copy/paste confirm — with Undo (5 s soft-delete window via existing storage trash path; new `trash` table, additive migration, auto-purge).
- Import/export/restore/backup: real `QProgressDialog` (cancelable) replacing wait-cursor-only; worker-thread paths already exist (`BackupService`, import transaction) — hook progress signals.
- Skeleton rows in list + shimmer in preview while `fetchPage`/OCR pending; `Reduce motion` turns shimmer into static placeholder.

**U12 — Motion language ✅ delivered 2026-09-23** (extends Track M M4; true positional motion via `UiHelpers::animate()`; 5 `tst_uidesign` slots, 30/30 + `--smoke` OK)
- Keep 120 ms cap; add: drawer slide (80 ms), chip fade/scale (80 ms), toast slide-up (120 ms) — all skipped under `Reduce motion`. One `UiHelpers::animate()` entry; no per-widget durations.

**U13 — Empty / error / offline states ✅ delivered 2026-09-23** (3 `tst_uidesign` slots, 30/30 + `--smoke` OK)
- One `makeEmptyState` everywhere: no-entries-yet (with shortcut hints), no-match (with "Clear filters"), group-empty, snippet-empty (already hinted — unify), OCR-missing (with install hint), DB-error (with integrity-check action). Each has icon + title + one action, never bare text.

**U16 — Capture feedback: sound + notification, top-aware ✅ delivered 2026-09-22** (settings persist + dedup + no-feedback decision; suite + `--smoke` green)
- Goal: play a sound + show a notification on every *new* copy; stay silent when the copy is already at the top; re-copying an older entry moves it back to the top.
- Move-to-top is already the storage contract (`insertOrUpdate` dedup touch: `timestamp = MAX(...)`, `use_count + 1`, `entryTouched` → model moves row to 0 in trivial Newest view). This item keeps that contract and pins it with a regression test — no schema change.
- Top check in `ApplicationContext::onCaptured` (single funnel for both `ClipboardWatcher` and `WlrDataControlHelper` paths): before `insertOrUpdate`, read the newest row (`fetchPage({}, {}, 1)`) and compare `content_hash`. If equal → already-at-top: skip sound (and notification), still allow the touch (`use_count` bump, row stays at 0). Covers empty history (no top → feedback) and pinned rows (order is timestamp-based, pins ignored).
- Feedback, each behind its own setting (both default on, both `tr()`, both gated by global `notificationsEnabled` for the popup): `captureSoundEnabled` (`[Ui] CaptureSound`) → `QApplication::beep()` (zero new deps; themed sound via `egoboard.notifyrc` later, not in this item); `captureNotificationEnabled` (`[Ui] CaptureNotification`) → `KNotification::event("capture", ...)` with preview + source app, `CloseOnTimeout`, suppressed for own paste-back writes (never reach `onCaptured`), ignored apps, excluded-sensitive and paused capture.
- Settings UI: two checkboxes under General ▸ Tray & notifications ("Play a sound on new copy", "Show a notification on new copy") + diagnostics line; live via existing `SettingsManager::changed()`.
- Tests (offscreen): settings persist + defaults; `insertOrUpdate` twice with same hash bumps `use_count`/timestamp and keeps row 0; top-hash-equal → no-feedback decision (pure helper, no `KNotification` in tests); full suite + `--smoke` green. Boundary: no `QtWidgets`/`KF6` in `src/core`, no DB work off the GUI thread beyond existing paths.

---

## 6. Settings, onboarding, help

**U14 — Settings dialog ✅ delivered 2026-09-23** (`SettingsDialog`, `SettingsManager`, palette; portability 6 slots + search 3 slots + responsive 2 slots + reset 4 slots + profiles 3+1 slots; ctest 31/31 + `--smoke` OK; snippet narrow stacking stays open as G6 tail)
- Search box atop dialog filtering pages + highlighting matching knobs (open §3.7 item); per-page "Reset to defaults"; settings export/import JSON next to history backup.
- Responsive: sidebar → top tabs under 640 px width; pages scroll (`QScrollArea` already partial — finish); Storage page shows DB path + quota bar + integrity actions in one card.
- Profiles ("Work"/"Personal"): switch whole setting sets from palette (`>profile`); stored as named `KConfig` groups.

**U19 — Settings crash diagnosis and hardening (P0)** ✅ *delivered 2026-09-22* (`MainWindow::openSettings`, `SettingsDialog`, `tst_uidesign`; 24/24 pass, suite 29/29 + `--smoke` OK)
- All entry points (window action, tray `settingsRequested`, palette `settingsRequested`) funnel into `MainWindow::openSettings()`; the crash surface was the dialog's deferred `QtConcurrent` workers touching the GUI-owned `StorageManager`/`SettingsManager` off-thread (`sourceApps()` in the constructor, `stats()`/`ocrLanguage()`/`databasePath()` in `refreshDiagnostics`).
- Fix (root cause, non-blocking open preserved): DB/settings values are snapshotted on the owning thread before dispatch; workers run only the external `tesseract`/`kwin` probes and carry snapshots by value; app suggestions load synchronously in the deferred slot off the indexed `idx_entries_app` scan; delivery stays lifetime-safe (`QPointer` guard + queued `invokeMethod` on `qApp`).

**U20 — Crash report collection and reader toolkit (P1)** ✅ *delivered 2026-09-22* (`src/main.cpp`, `SettingsDialog`, `CrashReport`; 13-slot `tst_crashreport`, 30/30 + `--smoke` OK)
- QtCore-only `CrashReport` behind both surfaces (CLI/Settings parity by construction): bounded schema (`egoboard-crash-report` v1: versions/build ID/symbols, QPA/session, signal/thread, parsed frames + capped raw, redacted logs, KWin, tool inventory, included-sections), home-path redaction, tolerant forward-compatible reader, `renderSummary` shared by both ends (first Egoboard frame highlighted with `=>`).
- Real detection, no invented parsers: GNU build ID + `.debug_info` via a bounds-checked ELF walk (with RelWithDebInfo guidance), `coredumpctl` latest-dump locating, `journalctl --user` scoped logs, `gdb`/`addr2line` inventory — every missing tool degrades to a copy-pasteable manual command, root never required.
- Privacy before saving: collection never reads history.db/clipboard/blobs/keys/full-env (allowlisted session vars only); Settings shows a preview (sections + no-clipboard/no-upload note) with Save/Cancel; Open renders partial/malformed files safely.

**U15 — First-run + discoverability ✅ delivered 2026-09-23** (`tst_cheatsheet` 31/31 + `tst_tour` 5/5 + settings/palette slots; ctest 32/32 + `--smoke` OK; tray guidance covered by U18)
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

- **Wayland portal paste**: opt-in toggle with per-session consent explainer UI (what it does, how to revoke — delivered 2026-09-23 via `PortalPaster` + `portalPasteEnabled`); fallback notification path stays.
- **Screencast awareness**: status-bar/dot indicator + auto-blur (delivered 2026-09-23 via `ScreencastWatcher` + `PreviewPane::setScreencastActive`; best-effort PipeWire heuristic, graceful when absent).
- **KRunner**: live multi-line previews per keystroke plus result categories by content type (delivered 2026-09-23 via `setMatchCategory`; no preview-pane API exists — verified). Open tail: none on the runner side.
- **Tray**: tooltip + menu refresh and paused-state icon overlay are tracked in U18 (theme-safe, SNI/fallback parity).

**U18 — Tray UI refresh (P1)** ✅ *delivered 2026-09-22* (`TrayController`, `SettingsManager`, tray tests; 6 `tst_traycycle` slots 13/13, suite 29/29 + `--smoke` OK)
- `TrayMode` is live and authoritative: `auto` shows the icon while history exists, `always` keeps it, `hidden` parks the SNI item as `Passive` (fallback hidden) with hotkeys and the process untouched; re-evaluated on `SettingsManager::changed()` and on captures/clears, touching the surface only when the outcome flips.
- Menu rebuilt into groups from a pure `TrayMenuModel` (QtCore-only, shared by both surfaces so SNI and fallback always match): state header (pause + count), bounded lazy recents with type icons (`text-plain`/`text-html`/`image-x-generic`/`folder`), capped labels with source-app suffix, full meta tooltips and an explicit disabled empty state; quick actions (show/quick-paste/pause) and management (Settings/clear/Quit) with theme icons; labels unchanged.
- Tooltip and icon follow pause/resume/capture via storage signals; paused icon is `media-playback-paused` from the theme (no bundled artwork), icon pushes only on state flips; `m_pauseAction` can no longer dangle across rebuilds.

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
| **R0 — Foundations** ✅ *landed 2026-09-20* | Tokens + helpers + tests | U4, U5 (24/32/44 targets, focus ring, elevation, skeleton/toast/chip/drawer metrics, breakpoints 720/1100, shared popup cards, font-relative sizing; 6 `tst_uidesign` 18/18, suite 29/29 + `--smoke` OK) |
| **R1 — Responsive shell** ✅ *landed 2026-09-20* | Breakpoints, wrapping filter bar, toolbar overflow | U1, U2, U3 (splitter↔drawer reparent, `Filters (n)` menu, `More` overflow; `tst_uidesign` 19/19, suite 29/29 + `--smoke` OK) |
| **R2 — List & preview** ✅ *landed 2026-09-20* | Chips, bulk bar, day headers, drawer preview, zoom/edit | U6, U7 (`tst_uidesign` 21/21, suite 29/29 + `--smoke` OK) |
| **R3 — Popups 2.0** ✅ *landed 2026-09-20* | Quick-paste search + two-line rows + monitor memory; palette rows + recents | U8, U9 (`tst_paletteui` 15/15, suite 29/29 + `--smoke` OK) |
| **R4 — Feedback & states ✅** *landed 2026-09-23* | Skeletons, empty-state/toast/motion foundations, capture sound+notification, bulk image export, shared motion, empty/error states, trash-based undo and cancelable progress | **U11** ✅, **U12** ✅, **U13** ✅, **U16** ✅, **U17** ✅ |
| **R5 — Settings & onboarding ✅** *landed 2026-09-23* | Settings search + reset + profiles + responsive dialog; settings JSON portability; per-page reset; profiles; first-run tour; cheatsheet; Settings crash hardening and crash-report toolkit | **U14** ✅, **U15** ✅, **U19** ✅, **U20** ✅ |
| **R6 — Platform polish ✅** *landed 2026-09-23* | Portal-paste consent, screencast blur + indicator, KRunner categories, tray UI refresh | **U18** ✅, §8 |
| **Later (P2 decided 2026-09-24)** | Dashboard → `.zip` backups → CopyQ `.cpq` import; semantic search / LAN-sync / browser companion out | P2-A, P2-B, P2-C scoped above |

> R0–R2 are sequential (shell before surfaces). R3–R6 can reorder by need — each ships independently. **U10/U11/U12/U13/U14/U15/U19 (P0), U17/U18/U20 (P1) are delivered**; R4 ✅, R5 ✅; remaining: manual Plasma Wayland checks (§8/R6) plus P2-A → P2-B → P2-C.

---

## 11. Non-goals

- No QML/Electron rewrite — Qt Widgets + KF6 stays.
- No cloud, no account, no telemetry, no AI exfiltration.
- No bundled themes/icons — runtime-discovered, read-only.
- No vacuum/DB work on the GUI thread; no hardcoded paths (use `QStandardPaths` + `SettingsManager`).

---

*Last updated: 2026-09-24 (U1–U20 delivered; R4 ✅, R5 ✅, R6 ✅; P0 complete; P1 ✅ 34/34 + `--smoke` OK; P2 horizon decided: dashboard → .zip → .cpq; semantic/LAN-sync/browser out; manual Plasma checks + G6 snippet stacking + G10 touch/HiDPI validation remain open) · P2 proposals are scoped-only by design — stopping here.*
