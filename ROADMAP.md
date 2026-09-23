# Egoboard — UI/UX Roadmap: Modern & Responsive

> Local-first clipboard history for KDE Plasma. This roadmap is UI/UX-only: make the existing Qt Widgets + KF6 app feel modern and responsive at any window size, on X11 and Wayland, without a rewrite, without cloud, without breaking the `src/core` (QtCore+Sql only) boundary.

**Status:** `v0.2` · Qt 6 + KF6 Widgets · local-only · MIT
**Revision 2026-09-23 (U14 per-page reset, partial):** Delivered the U14 per-page-reset slice: `SettingsManager::resetPageToDefaults()` restores one page (General/Capture/Privacy/History/SearchPreview/Automation/Storage) through the validating setters with a single `changed()` — session/placement/geometry state (quick-paste positions, window/splitter geometry, last filter, recents, `lastBackupMs`, sort/scope) and the encryption flag are never touched (encryption keeps its confirm + rekey flow, history data is kept); each of the 7 pages gains a `Reset this page to defaults` button (theme icon, accessible name/description, scope tooltip; Shortcuts keeps its existing hotkey reset, Diagnostics is read-only) that persists immediately then reloads widgets + diagnostics (General re-applies the theme pair live, Automation repopulates transform/snippet/script lists, active settings-search re-filters); 4 new `tst_settings` slots (General, Capture, Privacy/History/Search/Automation/Storage, single-emission + session preservation) — 38/38 pass, full ctest 31/31 + `--smoke` OK. Remaining U14: profiles. Previous revision notes preserved below.
**Revision 2026-09-23 (U15 cheatsheet, partial):** Delivered the U15 cheatsheet slice plus the missing §7 keys: `?` opens a new `ShortcutCheatsheet` dialog (system/window/popup/palette/timeline tables, data-only so it constructs anywhere), `/` focuses search, `Alt+1…5` jumps across search/list/preview/groups/timeline, `Esc` unwinds search text → filter chips → close, and the palette footer points at `?`; new `tst_cheatsheet` (sections coverage, full row build) — 31/31 pass + `--smoke` OK. Remaining U15: first-run tour. Previous revision notes preserved below.
**Revision 2026-09-23 (U10 timeline, partial):** Delivered the U10 timeline half: keyboard-navigable bars (arrows/Home/End move the day cursor from today, Enter/Space applies, Esc clears — same toggle semantics as clicks, Theme Highlight focus ring) plus per-bar accessible names ("12 entries, Monday") with press actions behind a `QAccessibleWidget` List + ListItem children (proxy identities so Qt's object-keyed cache can't collide bars with the parent — found and proven offscreen); shared `barRect()` keeps paint, hit-testing and AT geometry identical; 2 new `tst_uidesign` slots (keyboard move/activate/clear + names/roles/press/focus) — 30/30 pass + `--smoke` OK. Remaining U10: groups overlay drawer + drag count badge, narrow-combo variant. Previous revision notes preserved below.
**Revision 2026-09-23 (U14 responsive, partial):** Delivered the U14 responsive-narrow slice: under the 640 px token the sidebar becomes a horizontal top strip (layout direction + flow + scroll policies via shared `UiHelpers::applySidebarMode`, mode flips only, width clamp released/restored) and the Diagnostics page joins every other page inside the shared scroll area; pure token + helper pinned by 2 new `tst_uidesign` slots — 30/30 pass + `--smoke` OK. Remaining U14: per-page reset, profiles. Previous revision notes preserved below.
**Revision 2026-09-23 (U14 search, partial):** Delivered the U14 settings-search slice: a search box atop the dialog filters the 9-page sidebar (translated labels plus harvested knob texts — labels, buttons, group titles, placeholders, tooltips, HTML-stripped — so no keyword table and translations work) and bolds the matching knobs on the visible page (bold changes no color, contrast floors hold by construction), with match-count hint, pre-search row restore, and re-filter after deferred list population; pure `UiHelpers` helpers (`collectSettingTexts`, `settingQueryMatches`, `firstSettingMatchRow`) pinned by 3 new `tst_uidesign` slots — 30/30 pass + `--smoke` OK. Remaining U14: per-page reset, responsive narrow layout, profiles. Previous revision notes preserved below.
**Revision 2026-09-23 (U14 portability, partial):** Delivered the U14 settings-portability slice: `SettingsManager::exportToJson` / `importFromJson` (`egoboard-settings` v1 envelope, one key per setting, round-trips through the validating setters with a single `changed()`; per-screen quick-paste positions, expire rules, recents, base64 geometry included; `lastBackupMs` schedule state excluded; unknown keys ignored, newer versions rejected) plus Export/Import settings buttons next to the history backup (with dialog refresh); 6 new `tst_settings` slots (full round-trip, unknown/missing keys, bad files, schedule exclusion, positions replace, single emission) — 30/30 pass + `--smoke` OK. Remaining U14: search, per-page reset, responsive narrow layout, profiles. Previous revision notes preserved below.
**Revision 2026-09-23 (U11):** Delivered U11 (P0 toasts + undo + progress): cooperative cancel/progress across JSON/CSV/Markdown/HTML export (paged keyset gather via shared `fetchPayloadBatch`, no `fetchAllFull`), JSON/Klipper import (rollback on cancel, no reset signal), and backup/restore (worker atomic + progress signals, cancelable dialogs in Settings); MainWindow export + Settings export/import/Klipper run chunked on the GUI thread with Cancel (no worker-thread SQL); 8 new `tst_exportimport` slots (progress monotonicity, pre/mid-run cancel, rollback atomicity, Klipper/backup cancel) — 30/30 pass + `--smoke` OK + `--bench=100000` within budgets. R4 is complete. Previous revision notes preserved below.
**Revision 2026-09-23 (U13):** Delivered U13 (P0 empty/error states): every view is icon + title + one action — groups overlay gains a `New group` action (and now shows on first open, not just after model signals), the no-match state's `Clear filters` resets all filters at once via `MainWindow::clearAllFilters()`, the no-entries hint names the `Meta+V`/`Ctrl+K` shortcuts, preview images name the OCR state via pure `UiHelpers::ocrMetaSuffix()` (stored text, processing note, or tesseract install hint), and integrity failures persist as a Storage-page error state with a `Rebuild search index` action instead of a transient box only; 3 new `tst_uidesign` slots (action click-through, OCR matrix, groups action show/hide) — 30/30 pass + `--smoke` OK. Previous revision notes preserved below.
**Revision 2026-09-23 (U12):** Delivered U12 (P0 shared motion): `UiHelpers::animate()` now runs true positional motion — popup fade, 80 ms drawer slide, 80 ms chip fade+scale, 120 ms toast rise — from the single entry point with token durations and `Reduce motion` skip; child widgets fade via opacity effects (windowOpacity is window-only) and slides/scales end back on layout geometry; shadows preserved; 5 new `tst_uidesign` slots (durations, reduce-skip, chip, toast, drawer) — 30/30 pass + `--smoke` OK. Previous revision notes preserved below.
**Revision 2026-09-22 (U20):** Delivered U20 (P1 crash-report toolkit): QtCore-only `CrashReport` (bounded schema, redaction, gdb/ELF parsers) driving both `--crash-report` / `--read-crash-report` and Settings ▸ Diagnostics create/open-with-preview flows; 13-slot `tst_crashreport` (schema, redaction, fixtures, bounds, parity) — 30/30 pass + `--smoke` OK. Previous revision notes preserved below.
**Revision 2026-09-22 (U18):** Delivered U18 (P1 tray UI refresh): TrayMode is live (auto/always/hidden re-evaluated on settings changes and captures, no restart), menu rebuilt into state/recent/quick-action/management groups with type-aware icons, image-friendly rows and an explicit empty state, pause-aware tooltip + theme-only paused icon on both SNI and fallback surfaces; pure `TrayMenuModel` pinned by 6 new `tst_traycycle` slots (13/13 pass, full suite 29/29 + `--smoke` OK). Previous revision notes preserved below.
**Revision 2026-09-22 (U17):** Delivered U17 (P1 bulk image export): `ExportImportManager::exportImages()` streams bounded 200-row pages into a folder of collision-safe PNGs + `manifest.json` (metadata only unless text is opted in), with explicit sensitive policy, cancelable progress and no-overwrite guarantees; wired to the bulk-action bar and `>export images`; 10 new `tst_exportimport` slots (26/26 pass, full suite 29/29 + `--smoke` OK). Previous revision notes preserved below.
**Revision 2026-09-22 (U19):** Delivered U19 (P0 Settings crash diagnosis and hardening): the deferred `QtConcurrent` workers no longer touch GUI-owned storage/settings off-thread — DB values are snapshotted on the GUI thread, workers run only external probes, app suggestions load synchronously off the indexed scan, and `tst_uidesign` gains 3 open/close regression slots (24/24 pass, full suite 29/29 + `--smoke` OK). Previous revision note preserved below.
**Revision 2026-09-22:** Full-repo re-read (`src/core`, `src/app`, `src/app/ui` × 15 widgets, `tests/` × 29, `docs/`, packaging). Prior roadmap (Phases 1–9, Tracks A–M) archived in git history (`git log -- ROADMAP.md`) — its delivered work (Search 2.0, backups/restore, palette commands, KRunner actions, tray clicks/wheel, Track M design tokens) remains the baseline. This revision adds the requested bulk image export, tray UI refresh, Settings crash diagnosis, and crash-report collection/reader toolkit. What follows remains the single forward plan, focused on modern responsive UI/UX and the new stability/data-portability work.

**Repository review and roadmap update (2026-09-23):** Reviewed all tracked project surfaces: `src/core`, `src/app` and UI, KRunner, all test sources, build configuration, desktop metadata, build/package scripts, and user/build documentation. The detailed prior roadmap below remains intact as the specification and delivery ledger. This concise forward plan consolidates its open work and adds repository-wide maintenance work grounded in the current architecture. Theme implementation and theme resource files are intentionally left untouched.

### Forward roadmap — repository-wide priorities

**Current architecture and baseline**

- Native C++20 / Qt 6.8+ / KF6 application, with SQLite-backed `egoboard_core` separated from the desktop runtime and `ApplicationContext` as composition root. `GroupTreeModel` is the documented GUI-dependent exception compiled into the app.
- Core capabilities include WAL-backed deduplicated history, schema migrations, FTS and filtering, keyset paging, snippets/transforms, groups/bookmarks, import/export, sensitive-data rules, expiry, and background vacuuming. App integration covers clipboard capture, X11/Wayland metadata and paste paths, shortcuts, tray, D-Bus, KRunner, OCR, optional SQLCipher/KWallet, themes, settings, backups, and crash reports.
- UI includes responsive history/preview, quick paste, command palette, groups, timeline, settings, and export/import dialogs. The repository has broad QtTest coverage plus `--smoke` and `--bench`; CMake supports optional integrations and AppImage scripts. Build instructions and README are concise and do not yet describe the full shipped feature set.
- Preserve the established constraints: Qt Widgets/KF6, local-only operation, no bundled themes/icons, GUI-free core, additive data-safe migrations, parameterized SQL, Wayland-safe paste behavior, and existing theme discovery/application behavior.

**P0 — Finish the already-started user-facing work**

1. **R4 feedback and states ✅ delivered 2026-09-23:** trash-based undo for deletes, clear-history and expiry sweeps (exact-ID restore, no snapshot spikes); cancellable progress for backup/import/export/restore (cooperative cancel + progress, rollback on abort); non-blocking loading placeholders; U12 motion and U13 empty/error states. Keep `Reduce motion` and accessibility behavior throughout.
2. **R5 settings and onboarding:** complete U14 settings search, per-page reset, responsive narrow layout, storage status/quota/integrity presentation, and named profiles (settings JSON portability, dialog search, responsive narrow, and per-page reset delivered 2026-09-23); complete U15 first-run tour, shortcut cheatsheet, and last-capture tray guidance (cheatsheet + §7 keys delivered 2026-09-23; tray guidance already covered by U18). Retain the delivered thread-safe diagnostics and crash-report flows (U19/U20).
3. **R6 platform polish:** provide the opt-in Wayland portal paste consent/revocation flow, best-effort screencast status/blur, and KRunner live preview/result categories. U18 tray refresh is complete; retain parity across SNI and fallback surfaces.
4. **U10 and accessibility tail:** finish keyboard navigation/accessible labels for timeline and groups (timeline keyboard + per-bar names delivered 2026-09-23), narrow groups overlay/empty state/count feedback, touch scrolling, and pseudo-long translation layout checks. Keep high-contrast and keyboard-first behavior as acceptance requirements.

**P1 — Reliability, portability, and maintainability**

5. **Document the actual product:** expand README and build/package docs to cover shipped capture types, search syntax, privacy/encryption options, backups/import/export, shortcuts, KRunner/D-Bus, optional dependencies, platform differences, and diagnostics. Keep claims aligned with runtime feature detection and optional build flags.
6. **Platform and packaging verification:** establish a repeatable release checklist for X11 and Plasma Wayland, optional-feature builds (KRunner, layer shell, XTest, SQLCipher), installed desktop entry/autostart behavior, and AppImage validation. Record graceful behavior when optional protocols, tools, services, or plugins are absent.
7. **Data safety and recovery UX:** make backup/restore/import cancellation, progress, integrity results, and failure recovery understandable in the UI; preserve forward-only migrations and validate compatibility with older exported/backup data. Keep large-data operations bounded and avoid GUI-thread database work.
8. **Privacy controls and diagnostics:** keep sensitive-data handling explicit across capture, previews, notifications, exports, backups, and crash reports; make effective settings and optional subsystem availability discoverable without exposing clipboard contents or secrets.
9. **Integration regression coverage:** add focused automated coverage for platform/fallback decisions and packaging metadata where they can be tested headlessly; retain manual Plasma X11/Wayland checks for compositor-dependent behavior. Do not weaken existing assertions or the core/app boundary.

**P2 — Long-term product extensions**

10. Evaluate semantic search UI, LAN-sync pairing, browser companion, usage dashboard, CopyQ `.cpq` import, and `.zip` backups as separate scoped proposals. Define privacy, compatibility, dependency, migration, and performance requirements before implementation; each must fit the local-first model and receive its own UI/architecture review.

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
| ◐ G11 | Settings search, per-page reset delivered 2026-09-23 (filter + bold, 7 page-reset buttons); first-run tour still open (§3.7) | `SettingsDialog`, `SettingsManager.cpp` |
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

**U10 — Timeline + Groups ◐ partial** (`TimelineStrip`, `GroupsDock`)
- Timeline: keyboard-navigable bars (←/→ + Enter), accessible names per bar ("12 entries, Monday"), collapses to a combo under `Narrow`.
- Groups: overlay drawer mode under `Medium`; drop-target highlight already done — add count badge on drag (reuse quick-paste badge language); empty state with "New group" action.
- 2026-09-23 progress (timeline delivered): StrongFocus + arrows/Home/End day cursor, Enter/Space/Esc with click-identical toggle semantics, Highlight focus ring, `QAccessibleWidget` List with per-bar ListItem children (name + press action; proxy identities fix a proven object-keyed cache collision); shared `barRect()` for paint/hit-test/AT; 2 new `tst_uidesign` slots — full suite 30/30 + `--smoke` OK. Remaining: groups overlay drawer + drag count badge (empty action done in U13), narrow-combo variant.

**U17 — Bulk image export (P1)** ✅ *delivered 2026-09-22* (`ExportImportManager`, `ExportImportDialogs`, bulk-action bar)
- `exportImages()` covers selection / current filter / everything / pinned / group-subtree scopes: bounded 200-row keyset streaming plus single parameterized IN-query payload batches (never `fetchAllFull`), deterministic `egoboard-<timestamp>-<id>-<hash8>.png` names with `-n` collision suffixes and `QIODevice::NewOnly` so existing files are never overwritten, and a `manifest.json` (source app/window, time, pinned/sensitive, tags, OCR; payload text only with explicit opt-in).
- Explicit sensitive policy (skip-and-report by default), per-batch progress callback + atomic cancel (chunked synchronous run on the GUI thread — no worker-thread SQL), cancel/write-failure writes no manifest and reports how far the run got; history is never modified and JSON/Markdown/CSV/HTML exports are unchanged.
- UI: bulk-bar Export opens an `ImageExportDialog` (scope radios, folder picker, sensitive/text checkboxes, privacy hint) with cancelable progress and a result breakdown; `>export images` (palette completion + usage) routes to the same flow.
- Scale probe: 5000 images × 20 KB (100 MB) in ~0.6 s (~8500 img/s); 10 new `tst_exportimport` slots (selection/filter/pinned/group, skipped blobs, sensitive default + opt-in, collisions incl. pre-existing files, cancel, text opt-in, unwritable target) — 26/26 pass, full suite 29/29 + `--smoke` OK.
- Follow-up (format choice): PNG writes blobs verbatim; JPEG converts via a caller-provided `ImageEncoder` hook (core stays GUI-free, QImage conversion in `ExportImportDialogs::encodeImageForExport` with white-flattened alpha + quality 1–100), dialog format combo, manifest records `fileFormat`/`jpegQuality`; covered by encoder-hook slots plus real round-trip/reject/dialog-default slots in `tst_uidesign`.

---

## 5. Feedback, motion, states

**U11 — Toasts + undo ✅ delivered 2026-09-23** (`UiHelpers::makeToast`, `MainWindow`)
- Non-modal bottom toast for: delete entry, bulk delete, clear history, expire sweep, copy/paste confirm — with Undo (5 s soft-delete window via existing storage trash path; new `trash` table, additive migration, auto-purge).
- Import/export/restore/backup: real `QProgressDialog` (cancelable) replacing wait-cursor-only; worker-thread paths already exist (`BackupService`, import transaction) — hook progress signals.
- Skeleton rows in list + shimmer in preview while `fetchPage`/OCR pending; `Reduce motion` turns shimmer into static placeholder.
- 2026-09-23 delivered (undo): trash tables + `StorageManager` trash API + MainWindow delete/clear rewiring (exact-ID restore, no snapshot spikes) + expire-sweep undo toast via `ExpireScheduler::takeLastExpiredIds()` (toast when the window is visible, `KNotification` otherwise); 1 h retention purged at open and on soft-delete; 8 new `tst_storage` slots — full suite 30/30 + `--smoke` OK + `--bench=100000` within budgets.
- 2026-09-23 delivered (progress, closes R4): cooperative cancel + `(done, total)` progress across JSON/CSV/Markdown/HTML export (paged keyset gather through shared `fetchPayloadBatch`, per-64 serialize reports; cancel writes no file), JSON/Klipper import (cancel rolls the bulk transaction back, no reset signal), and backup/restore (`BackupWorker` atomic + progress signals, cancelable Settings dialogs); MainWindow export and Settings export/import/Klipper run chunked on the GUI thread with event pumping (no worker-thread SQL); 8 new `tst_exportimport` slots (progress monotonicity/reach-total, pre/mid-run cancel, Overwrite rollback atomicity, Klipper/backup cancel) — full suite 30/30 + `--smoke` OK + `--bench=100000` within budgets.

**U12 — Motion language ✅ delivered 2026-09-23** (extends Track M M4)
- Keep 120 ms cap; add: drawer slide (80 ms), chip fade/scale (80 ms), toast slide-up (120 ms) — all skipped under `Reduce motion`. One `UiHelpers::animate()` entry; no per-widget durations.
- 2026-09-23 progress: filter chips now use the 80 ms `Chip` kind; the Medium preview drawer invokes the 80 ms shared `SlideSide` kind when opened. Both honor `Reduce motion`. The shared implementation still animates opacity only, so true positional drawer/toast motion and chip scale remain open.
- 2026-09-23 delivered: `animate()` runs true motion — `Fade` keeps window-opacity for popups and opacity-effect fades for layout children; `Chip` fades plus a layout-stable geometry pulse; `SlideUp`/`SlideSide` fade plus a 16 px rise / 24 px side slide ending back on layout geometry. `Reduce motion` restores opacity without clearing drop shadows. Offsets reuse spacing tokens. 5 new `tst_uidesign` slots (durations, reduce-skip, chip, toast, drawer) — full suite 30/30 + `--smoke` OK.

**U13 — Empty / error / offline states ✅ delivered 2026-09-23**
- One `makeEmptyState` everywhere: no-entries-yet (with shortcut hints), no-match (with "Clear filters"), group-empty, snippet-empty (already hinted — unify), OCR-missing (with install hint), DB-error (with integrity-check action). Each has icon + title + one action, never bare text.
- 2026-09-23 delivered: groups overlay carries a `New group` action and paints on first open (initial `updateEmptyState()` — previously signals-only, so fresh installs saw a blank tree); no-match `Clear filters` runs `MainWindow::clearAllFilters()` (combos + group + toggles + timeline + search, one refresh; sort untouched) instead of clearing only the search box; no-entries subtitle names `Meta+V`/`Ctrl+K`; `UiHelpers::ocrMetaSuffix()` (pure, hermetic) drives the image footer — stored text, `processing…` note, or tesseract install hint; Settings ▸ Storage integrity failures persist as an inline error state with a `Rebuild search index` action. Snippet-empty keeps its adjacent Create form as the action; palette no-match stays a guidance hint line (popup, not a view). 3 new `tst_uidesign` slots — full suite 30/30 + `--smoke` OK.

**U16 — Capture feedback: sound + notification, top-aware ✅ delivered 2026-09-22**
- Goal: play a sound + show a notification on every *new* copy; stay silent when the copy is already at the top; re-copying an older entry moves it back to the top.
- Move-to-top is already the storage contract (`insertOrUpdate` dedup touch: `timestamp = MAX(...)`, `use_count + 1`, `entryTouched` → model moves row to 0 in trivial Newest view). This item keeps that contract and pins it with a regression test — no schema change.
- Top check in `ApplicationContext::onCaptured` (single funnel for both `ClipboardWatcher` and `WlrDataControlHelper` paths): before `insertOrUpdate`, read the newest row (`fetchPage({}, {}, 1)`) and compare `content_hash`. If equal → already-at-top: skip sound (and notification), still allow the touch (`use_count` bump, row stays at 0). Covers empty history (no top → feedback) and pinned rows (order is timestamp-based, pins ignored).
- Feedback, each behind its own setting (both default on, both `tr()`, both gated by global `notificationsEnabled` for the popup): `captureSoundEnabled` (`[Ui] CaptureSound`) → `QApplication::beep()` (zero new deps; themed sound via `egoboard.notifyrc` later, not in this item); `captureNotificationEnabled` (`[Ui] CaptureNotification`) → `KNotification::event("capture", ...)` with preview + source app, `CloseOnTimeout`, suppressed for own paste-back writes (never reach `onCaptured`), ignored apps, excluded-sensitive and paused capture.
- Settings UI: two checkboxes under General ▸ Tray & notifications ("Play a sound on new copy", "Show a notification on new copy") + diagnostics line; live via existing `SettingsManager::changed()`.
- Tests (offscreen): settings persist + defaults; `insertOrUpdate` twice with same hash bumps `use_count`/timestamp and keeps row 0; top-hash-equal → no-feedback decision (pure helper, no `KNotification` in tests); full suite + `--smoke` green. Boundary: no `QtWidgets`/`KF6` in `src/core`, no DB work off the GUI thread beyond existing paths.

---

## 6. Settings, onboarding, help

**U14 — Settings dialog ◐ partial** (`SettingsDialog`)
- Search box atop dialog filtering pages + highlighting matching knobs (open §3.7 item); per-page "Reset to defaults"; settings export/import JSON next to history backup.
- Responsive: sidebar → top tabs under 640 px width; pages scroll (`QScrollArea` already partial — finish); Storage page shows DB path + quota bar + integrity actions in one card.
- Profiles ("Work"/"Personal"): switch whole setting sets from palette (`>profile`); stored as named `KConfig` groups.
- 2026-09-23 progress (portability delivered): full-preference JSON snapshot with validating round-trip, single-`changed()` import, machine-local exclusions and forward-compatible reads; Export/Import buttons on the Storage page next to the history backup (encryption keys stay in KWallet, never enter the file); 6 new `tst_settings` slots — full suite 30/30 + `--smoke` OK.
- 2026-09-23 progress (search delivered): search box atop the dialog filters the sidebar (translated labels + harvested knob texts, no keyword table) and bolds matching knobs with contrast-safe weight-only highlight, match-count hint, pre-search row restore, re-filter after deferred population; 3 new `tst_uidesign` slots — full suite 30/30 + `--smoke` OK.
- 2026-09-23 progress (responsive delivered): under-640 px sidebar → top strip via `applyResponsiveLayout()` + shared `applySidebarMode()` (single token source with the tests, flip-only switching, clamp release/restore); Diagnostics wrapped in the shared scroll area so all 9 pages scroll; 2 new `tst_uidesign` slots — full suite 30/30 + `--smoke` OK. Remaining: per-page reset, profiles. Snippet narrow stacking stays open (G6 tail).
- 2026-09-23 progress (per-page reset delivered): `SettingsManager::resetPageToDefaults()` restores one page through the validating setters with a single `changed()` (session/placement/geometry state and the encryption flag excluded by design); each of the 7 settings pages carries a `Reset this page to defaults` button (Shortcuts keeps its hotkey reset, Diagnostics stays read-only) that reloads widgets + diagnostics after persisting; 4 new `tst_settings` slots — 38/38 pass, full ctest 31/31 + `--smoke` OK. Remaining: profiles. Snippet narrow stacking stays open (G6 tail).

**U19 — Settings crash diagnosis and hardening (P0)** ✅ *delivered 2026-09-22* (`MainWindow::openSettings`, `SettingsDialog`, `tst_uidesign`)
- All entry points (window action, tray `settingsRequested`, palette `settingsRequested`) funnel into `MainWindow::openSettings()`; the crash surface was the dialog's deferred `QtConcurrent` workers touching the GUI-owned `StorageManager`/`SettingsManager` off-thread (`sourceApps()` in the constructor, `stats()`/`ocrLanguage()`/`databasePath()` in `refreshDiagnostics`).
- Fix (root cause, non-blocking open preserved): DB/settings values are snapshotted on the owning thread before dispatch; workers run only the external `tesseract`/`kwin` probes and carry snapshots by value; app suggestions load synchronously in the deferred slot off the indexed `idx_entries_app` scan; delivery stays lifetime-safe (`QPointer` guard + queued `invokeMethod` on `qApp`).
- Regression in `tst_uidesign` (same `QPointer` + queued-handoff shape against the real `StorageManager`, populated and empty DBs): snapshots never touch storage off-thread, early-close delivery no-ops, 20× open/close stress + live handoff — 24/24 pass, full suite 29/29 + `--smoke` OK.

**U20 — Crash report collection and reader toolkit (P1)** ✅ *delivered 2026-09-22* (`src/main.cpp`, `SettingsDialog`, `CrashReport`, diagnostics tooling)
- QtCore-only `CrashReport` behind both surfaces (CLI/Settings parity by construction): bounded schema (`egoboard-crash-report` v1: versions/build ID/symbols, QPA/session, signal/thread, parsed frames + capped raw, redacted logs, KWin, tool inventory, included-sections), home-path redaction, tolerant forward-compatible reader, `renderSummary` shared by both ends (first Egoboard frame highlighted with `=>`).
- Real detection, no invented parsers: GNU build ID + `.debug_info` via a bounds-checked ELF walk (with RelWithDebInfo guidance), `coredumpctl` latest-dump locating, `journalctl --user` scoped logs, `gdb`/`addr2line` inventory — every missing tool degrades to a copy-pasteable manual command, root never required.
- Privacy before saving: collection never reads history.db/clipboard/blobs/keys/full-env (allowlisted session vars only); Settings shows a preview (sections + no-clipboard/no-upload note) with Save/Cancel; Open renders partial/malformed files safely.
- Verified live: `--crash-report` wrote + `--read-crash-report` re-rendered a real bundle (incl. a genuine prior SIGSEGV dump entry from the journal); error paths exit 1 with reasons. 13-slot `tst_crashreport` (schema/round-trip/forward-compat, malformed fixtures, redaction, no-payload allowlist, signals, gdb fixture + highlight + garbage cap, ELF fixtures incl. truncated/foreign, log/frame bounds, hermetic + shared-renderer parity, tool-degradation) — full suite 30/30 + `--smoke` OK.

**U15 — First-run + discoverability ◐ partial**
- 4-step overlay tour (hotkeys, palette, privacy, settings search) on first launch only; skippable, never re-shows without asking.
- Shortcut cheatsheet (`?` in main window + palette footer); tray tooltip shows pause state + last capture time.
- 2026-09-23 progress (cheatsheet + §7 keys delivered): `ShortcutCheatsheet` dialog over data-only sections with the documented map (system defaults as in `HotkeyManager`, reconfigurable note included); `/` focuses search, `Alt+1…5` focus areas (with `GroupsDock::focusTree()` and a StrongFocus preview pane), `Esc` unwinds search → chips (`clearAllFilters`) → close; palette empty-hint advertises `?`; new `tst_cheatsheet` — full suite 31/31 + `--smoke` OK. Remaining: first-run tour (tray guidance already covered by U18's pause-aware + last-capture tooltip).

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

**U18 — Tray UI refresh (P1)** ✅ *delivered 2026-09-22* (`TrayController`, `SettingsManager`, tray tests)
- `TrayMode` is live and authoritative: `auto` shows the icon while history exists, `always` keeps it, `hidden` parks the SNI item as `Passive` (fallback hidden) with hotkeys and the process untouched; re-evaluated on `SettingsManager::changed()` and on captures/clears, touching the surface only when the outcome flips.
- Menu rebuilt into groups from a pure `TrayMenuModel` (QtCore-only, shared by both surfaces so SNI and fallback always match): state header (pause + count), bounded lazy recents with type icons (`text-plain`/`text-html`/`image-x-generic`/`folder`), capped labels with source-app suffix, full meta tooltips and an explicit disabled empty state; quick actions (show/quick-paste/pause) and management (Settings/clear/Quit) with theme icons; labels unchanged.
- Tooltip and icon follow pause/resume/capture via storage signals; paused icon is `media-playback-paused` from the theme (no bundled artwork), icon pushes only on state flips; `m_pauseAction` can no longer dangle across rebuilds.
- Offscreen probe (real controller + temp managers): insert/pause/mode-change/clear with zero runtime warnings; 6 new `tst_traycycle` slots (mode matrix, header, tooltip, empty, rows, meta) — 13/13 pass, full suite 29/29 + `--smoke` OK.

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
| **R4 — Feedback & states ✅** *landed 2026-09-23* | Skeletons, empty-state/toast/motion foundations, capture sound+notification, bulk image export, shared motion, empty/error states, trash-based undo and cancelable progress | **U11** ✅, **U12** ✅, **U13** ✅, **U16** ✅, **U17** ✅ |
| **R5 — Settings & onboarding** ◐ *partial (U19/U20 landed 2026-09-22)* | Settings search + reset + profiles + responsive dialog; settings JSON portability ✅ 2026-09-23; per-page reset ✅ 2026-09-23; first-run tour; cheatsheet ✅ 2026-09-23; Settings crash hardening ✅ and crash-report toolkit ✅ | U14, U15, **U19** ✅, **U20** ✅ |
| **R6 — Platform polish** | Portal-paste consent UI, screencast blur, KRunner preview, tray UI refresh ✅ | **U18** ✅, §8 |
| **Later** | Semantic search UI, LAN-sync pairing UI, browser companion, stats dashboard, CopyQ `.cpq` reader, `.zip` backups | Carried long-term; each needs its own UI pass against this system |

> R0–R2 are sequential (shell before surfaces). R3–R6 can reorder by need — each ships independently. **U11/U12/U13/U19 (P0), U17/U18/U20 (P1) are delivered**; remaining: U14 profiles, U15 tour, U10 groups tail, §8 platform items.

---

## 11. Non-goals

- No QML/Electron rewrite — Qt Widgets + KF6 stays.
- No cloud, no account, no telemetry, no AI exfiltration.
- No bundled themes/icons — runtime-discovered, read-only.
- No vacuum/DB work on the GUI thread; no hardcoded paths (use `QStandardPaths` + `SettingsManager`).

---

*Last updated: 2026-09-23 (status synced: U1–U13, U16–U20 delivered; R4 complete; U14 partial — portability + search + responsive + per-page reset done; U10 partial — timeline keyboard/AT done; U15 partial — cheatsheet done) · Next: U14 profiles, U15 tour, or U10-groups/R6 tails.*
