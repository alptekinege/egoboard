# Egoboard — UI/UX Roadmap: Modern & Responsive

> Local-first clipboard history for KDE Plasma. This roadmap is UI/UX-only: make the existing Qt Widgets + KF6 app feel modern and responsive at any window size, on X11 and Wayland, without a rewrite, without cloud, without breaking the `src/core` (QtCore+Sql only) boundary.

**Status:** `v0.2` · Qt 6 + KF6 Widgets · local-only · MIT
**Revision 2026-09-23 (P1 ✅):** Delivered the reliability/portability phase: README expanded to the actual product (shortcuts table, search syntax from the in-app hints, privacy/encryption, backups + JSON/CSV/Markdown/HTML/image exports + Klipper import, settings JSON + profiles, KRunner `eb` + D-Bus API with a working example, X11/Wayland platform notes incl. portal opt-in and screencast behavior, diagnostics + crash-report flows); `docs/build.md` gains the PipeWire optional dep and the runtime-detected fallback table; new `docs/release-checklist.md` (clean gates, X11 + Wayland session checks, optional-feature absence matrix, desktop/autostart, AppImage validation); new `tst_packaging` (desktop-entry validity, helper-script presence + exec bit); v1-export back-compat import slot in `tst_exportimport` (sparse keys import cleanly); privacy/diagnostics verified by review (explicit skip/redact notifications on both capture paths, no clipboard/secrets in crash reports or diagnostics, subsystem states discoverable in Settings + status UI). Full ctest 34/34 + `--smoke` OK. P2 proposals remain scoped-only, untouched. Previous revision notes preserved below.
**Revision 2026-09-23 (R6 screencast ✅):** Delivered the screencast-awareness slice, completing R6: `ScreencastWatcher` polls the local PipeWire registry (optional `EGOBOARD_HAVE_PIPEWIRE` flag — graceful stub otherwise) for portal/compositor capture sources with a conservative pure heuristic (portal/screencast-named Video nodes only; cameras, audio and app streams never count), one sweep at a time off the GUI thread with the U19 lifetime-safe handoff (a teardown race found by ctest and fixed with the QPointer guard); `MainWindow::setScreencastActive()` shows a lazily built status-bar indicator (theme icon + Sharing label, named, tooltip) only while sharing and drives `PreviewPane::setScreencastActive()`, whose overlay now hides every payload via pure `UiHelpers::shouldBlurPreview()` with the same hover/focus reveal; new `tst_screencast` (heuristic matrix, live-daemon negative with a real webcam present, inactive start) + 1 `tst_uidesign` slot (blur truth table) — 5/5 pass, full ctest 33/33 + `--smoke` OK. Best-effort limits disclosed: absence of a recognizable node reads as inactive, and the positive case needs a manual Plasma Wayland check. Remaining: P1 docs/verification, P2 proposals. Previous revision notes preserved below.
**Revision 2026-09-23 (R6 portal paste, partial):** Delivered the opt-in Wayland portal paste slice: `PortalPaster` asks the compositor to press Ctrl+V through org.freedesktop.portal RemoteDesktop (CreateSession → SelectDevices keyboard → Start with persist-until-revoked, then fire-and-forget NotifyKeyboardKeysym chord) with per-session consent — the compositor prompts on first use, a 30 s guard and any denial/error leave the session down, and pastes only ever go through a live session so the manual-paste notification fallback stays (never hangs, never double-pastes); `SettingsManager::portalPasteEnabled()` (default off, travels in JSON/profiles, resets with General) drives it via `AutoPaster` (`pasteMethod()` first-choice matrix: X11 → injection, Wayland → portal iff opted in and live) with startup + live-toggle sync in `ApplicationContext`; Settings ▸ General ▸ Pasting gains the opt-in checkbox, a what-it-does/how-to-revoke explainer, and a live availability status; 3 new `tst_autopaster` slots (decision matrix, keysym chord, absent-portal fast-fail) + 1 new `tst_settings` slot (default/persist/JSON/reset) — full ctest 32/32 + `--smoke` OK. The live portal handshake itself needs a compositor (manual Plasma Wayland check). Remaining R6: screencast blur. Previous revision notes preserved below.
**Revision 2026-09-23 (R6 KRunner categories, partial):** Delivered the KRunner result-categories slice: `EntryRow::matchCategory()`/`categoryForType()` groups rows by content type (plain + rich text share "text", images and files group on their own, unknown types fall back to text like the match icons), wired in `EgoboardRunner::match()` via the real `setMatchCategory()` API with translated labels (Text/Images/Files) and category relevance (text High, images/files Moderate, so the common case groups first); live multi-line previews per keystroke are unchanged; 1 new `tst_krunner` slot (category matrix incl. unknown/empty fallback) — 18/18 pass, full ctest 32/32 + `--smoke` OK. Note: KRunner exposes no preview-pane API (verified against the installed KF6 headers), so the per-keystroke multi-line match text stands as the live preview. Remaining R6: portal paste consent, screencast blur. Previous revision notes preserved below.
**Revision 2026-09-23 (§7 tail ✅):** Delivered the touch + pseudo-long tail: `UiHelpers::enableTouchScroll()` grabs `TouchGesture` on the history list viewport (mouse drags keep their DnD meaning; null-safe, harmless without touch hardware) and `UiHelpers::pseudoLong()` expands UI strings German-length (vowel doubling incl. umlauts, bracketed so truncation is visible) as a test probe; the tour dialog survives pseudo-long steps at default and 360 px widths with no wrapped label clipping; 3 new `tst_uidesign` slots (scroller grabbed + null-safe, expansion shape incl. umlauts, per-step clip check at both widths) — full ctest 32/32 + `--smoke` OK. Remaining: §8/R6 platform tails, then P1. Previous revision notes preserved below.
**Revision 2026-09-23 (U10 groups ✅):** Delivered the U10 groups + narrow tail, closing U10: `GroupsDock::setOverlayMode()` floats the dock as a drawer off Wide (drawer width follows the parent window via `DrawerWidthFraction`, clamped 240–420, idempotent, accessible description names dock vs overlay), driven by `MainWindow::applyResponsiveMode()`; drop-target rows now carry the dragged-entry count via `GroupTreeModel::entryCount()` (decoded entry-id list size, 0 for group/foreign/malformed payloads) painted as a Highlight/HighlightedText badge in the same language as the list drag pixmap; `TimelineStrip::dayOptions()`/`selectDay()`/`barDayStart()` feed a narrow combo (`All days` + labeled day ranges) that stays visible exactly while the strip is collapsed and drives the strip with click-identical toggle semantics (plus live `timelineEnabled` sync); 1 new `tst_grouptreemodel` slot (entry-count matrix) + 2 new `tst_uidesign` slots (day options/select/toggle/clear, overlay float/width/description/idempotence) — 14/14 + full `tst_uidesign` pass, full ctest 32/32 + `--smoke` OK. Remaining: §8/R6 platform tails. Previous revision notes preserved below.
**Revision 2026-09-23 (U15 tour ✅):** Delivered the first-run tour slice, completing U15 (and R5): data-only `FirstRunTour` dialog (4 steps — hotkeys, palette, privacy, settings search — icon + title + rich-text body + hint, step counter, Back/Next-turns-Finish/Skip, accessible names, palette-only colors, theme icons) shown once on first launch via `MainWindow::showEvent` → deferred `maybeShowFirstRunTour()` (per-run guard + `SettingsManager::tourSeen` flag set on any close: finish, skip or frame-close, so it never re-shows without asking); re-entry through the Tour toolbar action (collapses into More off Wide), palette `>tour`, and the `?` cheatsheet cross-links; `tourSeen` stays machine-local (never in settings export/import or profiles, like `lastBackupMs`); new `tst_tour` (4-step coverage, offscreen navigation/clamp/finish, show-once decision) + 1 `tst_settings` slot (default/persist/JSON exclusion) + 1 `tst_palette` slot (parse/resolve/suggest) — 5/5 + 42/42 + 11/11 pass, full ctest 32/32 + `--smoke` OK. Remaining: U10-groups/R6 tails. Previous revision notes preserved below.
**Revision 2026-09-23 (U14 profiles ✅):** Delivered the U14 profiles slice, completing U14: `SettingsManager` named profiles (`Work`/`Personal`) — each profile is its own `KConfig` group (`Profile <name>`) holding a JSON snapshot of `exportToJson()`, so switching routes through `importFromJson` with the same validation, machine-local exclusions (`lastBackupMs` stays local) and single `changed()`; history data and KWallet secrets never enter a profile; names are trimmed 1–40 printable chars without `/ \ [ ]`, apply resolves case-insensitively, save/delete sync without emissions, deleting the active profile clears it; the palette gains `>profile <name>` (new `Profile` argument with saved-name completion, profile-aware hints — unknown names warn instead of create) wired in `MainWindow::applyProfileByName()` with available-name warnings plus a switch confirmation; Settings ▸ Storage gains a Profiles row (editable combo + Save/Apply/Delete, active-profile selection, same reload + theme + diagnostics refresh as Import); 3 new `tst_settings` slots (save/apply/delete round-trip + persistence, bad-name/unknown rejection + case-insensitive apply, single-emission + schedule exclusion) and 1 new `tst_palette` slot (parse/resolve/suggest/complete) — 41/41 + 10/10 pass, full ctest 31/31 + `--smoke` OK. Remaining: U15 tour, U10-groups/R6 tails. Previous revision notes preserved below.
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
2. **R5 settings and onboarding ✅ delivered 2026-09-23:** U14 settings search, per-page reset, responsive narrow layout, storage status/quota/integrity presentation, and named profiles (JSON portability, dialog search, responsive narrow, per-page reset, profiles); U15 first-run tour, shortcut cheatsheet, and last-capture tray guidance (tour + cheatsheet + §7 keys; tray guidance covered by U18). Thread-safe diagnostics and crash-report flows retained (U19/U20).
3. **R6 platform polish ✅ delivered 2026-09-23:** opt-in Wayland portal paste consent/revocation flow (`PortalPaster` RemoteDesktop Ctrl+V behind `portalPasteEnabled`, per-session compositor consent, notification fallback always stays), best-effort screencast status/blur (`ScreencastWatcher` PipeWire probe + status-dot indicator + preview auto-blur), and KRunner live preview/result categories (content-type grouping with relevance ordering; KRunner has no preview-pane API — per-keystroke multi-line match text is the live preview). U18 tray refresh is complete; retain parity across SNI and fallback surfaces.
4. **U10 and accessibility tail ✅ done 2026-09-23:** keyboard navigation/accessible labels for timeline and groups (timeline keyboard + per-bar names; groups overlay drawer + drop count badge + narrow day combo), narrow groups overlay/empty state/count feedback, touch scrolling (`TouchGesture` on the history list viewport), and pseudo-long translation layout checks (German-length probe, tour clip-free at default + narrow widths). High-contrast and keyboard-first behavior stay acceptance requirements.

**P1 — Reliability, portability, and maintainability**

5. **Document the actual product ✅ delivered 2026-09-23:** README covers shipped capture types, search syntax, privacy/encryption options, backups/import/export (+ settings JSON, profiles), shortcuts, KRunner/D-Bus, optional dependencies, platform differences, and diagnostics; `docs/build.md` lists every optional build/runtime input with its fallback; claims verified against runtime feature detection and build flags.
6. **Platform and packaging verification ✅ delivered 2026-09-23:** `docs/release-checklist.md` repeats the gates for X11 and Plasma Wayland, the optional-feature matrix (KRunner, layer shell, XTest, SQLCipher, PipeWire) with expected absent behavior, installed desktop entry/autostart behavior, and AppImage validation; `tst_packaging` pins desktop-entry validity and helper-script presence headlessly.
7. **Data safety and recovery UX ✅ delivered 2026-09-23:** cancel/progress/integrity/failure UX shipped across U11/U13 (chunked GUI-thread runs, rollback on abort, persistent integrity state); forward-only migrations retained; v1-export back-compat import covered in `tst_exportimport`; large-data ops stay bounded off the GUI thread.
8. **Privacy controls and diagnostics ✅ verified 2026-09-23:** sensitive handling explicit across capture (skip/redact notifications on both paths), previews (blur + OCR state), notifications (gated, sensitive-suppressed), exports/backups (sensitive policy, no secrets in crash reports/diagnostics); effective settings and subsystem availability discoverable via Settings ▸ Diagnostics, portal/screencast/OCR status lines, and the sharing indicator.
9. **Integration regression coverage ✅ delivered 2026-09-23:** headless coverage for platform/fallback decisions (paste-method matrix, portal/screencast probes, layer-shell/data-control diagnostics) and packaging metadata (`tst_packaging`); compositor-dependent behavior stays manual per the release checklist.

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

**U10 — Timeline + Groups ✅ delivered 2026-09-23** (`TimelineStrip`, `GroupsDock`)
- Timeline: keyboard-navigable bars (←/→ + Enter), accessible names per bar ("12 entries, Monday"), collapses to a combo under `Narrow`.
- Groups: overlay drawer mode under `Medium`; drop-target highlight already done — add count badge on drag (reuse quick-paste badge language); empty state with "New group" action.
- 2026-09-23 progress (timeline delivered): StrongFocus + arrows/Home/End day cursor, Enter/Space/Esc with click-identical toggle semantics, Highlight focus ring, `QAccessibleWidget` List with per-bar ListItem children (name + press action; proxy identities fix a proven object-keyed cache collision); shared `barRect()` for paint/hit-test/AT; 2 new `tst_uidesign` slots — full suite 30/30 + `--smoke` OK. Remaining: groups overlay drawer + drag count badge (empty action done in U13), narrow-combo variant.
- 2026-09-23 progress (groups + narrow delivered, closes U10): overlay drawer via `GroupsDock::setOverlayMode()` off Wide (drawer-width fraction, idempotent, named accessible description); drop-row count badge from `GroupTreeModel::entryCount()` in the drag-pixmap badge language; narrow day combo from `TimelineStrip::dayOptions()` driving `selectDay()` with click-identical toggle semantics (live `timelineEnabled` sync in the settings-changed handler and `applyResponsiveMode`); 1 new `tst_grouptreemodel` slot + 2 new `tst_uidesign` slots — full ctest 32/32 + `--smoke` OK.

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

**U14 — Settings dialog ✅ delivered 2026-09-23** (`SettingsDialog`, `SettingsManager`, palette)
- Search box atop dialog filtering pages + highlighting matching knobs (open §3.7 item); per-page "Reset to defaults"; settings export/import JSON next to history backup.
- Responsive: sidebar → top tabs under 640 px width; pages scroll (`QScrollArea` already partial — finish); Storage page shows DB path + quota bar + integrity actions in one card.
- Profiles ("Work"/"Personal"): switch whole setting sets from palette (`>profile`); stored as named `KConfig` groups.
- 2026-09-23 progress (portability delivered): full-preference JSON snapshot with validating round-trip, single-`changed()` import, machine-local exclusions and forward-compatible reads; Export/Import buttons on the Storage page next to the history backup (encryption keys stay in KWallet, never enter the file); 6 new `tst_settings` slots — full suite 30/30 + `--smoke` OK.
- 2026-09-23 progress (search delivered): search box atop the dialog filters the sidebar (translated labels + harvested knob texts, no keyword table) and bolds matching knobs with contrast-safe weight-only highlight, match-count hint, pre-search row restore, re-filter after deferred population; 3 new `tst_uidesign` slots — full suite 30/30 + `--smoke` OK.
- 2026-09-23 progress (responsive delivered): under-640 px sidebar → top strip via `applyResponsiveLayout()` + shared `applySidebarMode()` (single token source with the tests, flip-only switching, clamp release/restore); Diagnostics wrapped in the shared scroll area so all 9 pages scroll; 2 new `tst_uidesign` slots — full suite 30/30 + `--smoke` OK. Remaining: per-page reset, profiles. Snippet narrow stacking stays open (G6 tail).
- 2026-09-23 progress (per-page reset delivered): `SettingsManager::resetPageToDefaults()` restores one page through the validating setters with a single `changed()` (session/placement/geometry state and the encryption flag excluded by design); each of the 7 settings pages carries a `Reset this page to defaults` button (Shortcuts keeps its hotkey reset, Diagnostics stays read-only) that reloads widgets + diagnostics after persisting; 4 new `tst_settings` slots — 38/38 pass, full ctest 31/31 + `--smoke` OK. Remaining: profiles. Snippet narrow stacking stays open (G6 tail).
- 2026-09-23 progress (profiles delivered, closes U14): named profiles (`Work`/`Personal`) as own `KConfig` groups (`Profile <name>`) holding an `exportToJson()` snapshot; switching routes through `importFromJson` (validation + machine-local exclusions + single `changed()`); palette `>profile <name>` with saved-name completion plus `MainWindow::applyProfileByName()` feedback; Settings ▸ Storage Profiles row (Save/Apply/Delete, active-profile selection); 3 new `tst_settings` slots + 1 new `tst_palette` slot — 41/41 + 10/10 pass, full ctest 31/31 + `--smoke` OK. Snippet narrow stacking stays open (G6 tail).

**U19 — Settings crash diagnosis and hardening (P0)** ✅ *delivered 2026-09-22* (`MainWindow::openSettings`, `SettingsDialog`, `tst_uidesign`)
- All entry points (window action, tray `settingsRequested`, palette `settingsRequested`) funnel into `MainWindow::openSettings()`; the crash surface was the dialog's deferred `QtConcurrent` workers touching the GUI-owned `StorageManager`/`SettingsManager` off-thread (`sourceApps()` in the constructor, `stats()`/`ocrLanguage()`/`databasePath()` in `refreshDiagnostics`).
- Fix (root cause, non-blocking open preserved): DB/settings values are snapshotted on the owning thread before dispatch; workers run only the external `tesseract`/`kwin` probes and carry snapshots by value; app suggestions load synchronously in the deferred slot off the indexed `idx_entries_app` scan; delivery stays lifetime-safe (`QPointer` guard + queued `invokeMethod` on `qApp`).
- Regression in `tst_uidesign` (same `QPointer` + queued-handoff shape against the real `StorageManager`, populated and empty DBs): snapshots never touch storage off-thread, early-close delivery no-ops, 20× open/close stress + live handoff — 24/24 pass, full suite 29/29 + `--smoke` OK.

**U20 — Crash report collection and reader toolkit (P1)** ✅ *delivered 2026-09-22* (`src/main.cpp`, `SettingsDialog`, `CrashReport`, diagnostics tooling)
- QtCore-only `CrashReport` behind both surfaces (CLI/Settings parity by construction): bounded schema (`egoboard-crash-report` v1: versions/build ID/symbols, QPA/session, signal/thread, parsed frames + capped raw, redacted logs, KWin, tool inventory, included-sections), home-path redaction, tolerant forward-compatible reader, `renderSummary` shared by both ends (first Egoboard frame highlighted with `=>`).
- Real detection, no invented parsers: GNU build ID + `.debug_info` via a bounds-checked ELF walk (with RelWithDebInfo guidance), `coredumpctl` latest-dump locating, `journalctl --user` scoped logs, `gdb`/`addr2line` inventory — every missing tool degrades to a copy-pasteable manual command, root never required.
- Privacy before saving: collection never reads history.db/clipboard/blobs/keys/full-env (allowlisted session vars only); Settings shows a preview (sections + no-clipboard/no-upload note) with Save/Cancel; Open renders partial/malformed files safely.
- Verified live: `--crash-report` wrote + `--read-crash-report` re-rendered a real bundle (incl. a genuine prior SIGSEGV dump entry from the journal); error paths exit 1 with reasons. 13-slot `tst_crashreport` (schema/round-trip/forward-compat, malformed fixtures, redaction, no-payload allowlist, signals, gdb fixture + highlight + garbage cap, ELF fixtures incl. truncated/foreign, log/frame bounds, hermetic + shared-renderer parity, tool-degradation) — full suite 30/30 + `--smoke` OK.

**U15 — First-run + discoverability ✅ delivered 2026-09-23**
- 4-step overlay tour (hotkeys, palette, privacy, settings search) on first launch only; skippable, never re-shows without asking.
- Shortcut cheatsheet (`?` in main window + palette footer); tray tooltip shows pause state + last capture time.
- 2026-09-23 progress (cheatsheet + §7 keys delivered): `ShortcutCheatsheet` dialog over data-only sections with the documented map (system defaults as in `HotkeyManager`, reconfigurable note included); `/` focuses search, `Alt+1…5` focus areas (with `GroupsDock::focusTree()` and a StrongFocus preview pane), `Esc` unwinds search → chips (`clearAllFilters`) → close; palette empty-hint advertises `?`; new `tst_cheatsheet` — full suite 31/31 + `--smoke` OK. Remaining: first-run tour (tray guidance already covered by U18's pause-aware + last-capture tooltip).
- 2026-09-23 progress (tour delivered, closes U15): `FirstRunTour` over data-only steps (hotkeys naming `Meta+V`/`?`, palette naming `Ctrl+K`/`>` commands, privacy naming never-store + Settings ▸ Privacy, settings search naming the search box + per-page reset + profiles); first `showEvent` offers it once via a deferred open (per-run guard; `tourSeen` set on finish/skip/close); Tour toolbar action (More off Wide) + palette `>tour` re-open it; `tourSeen` excluded from JSON portability and profiles; new `tst_tour` + 1 `tst_settings` slot + 1 `tst_palette` slot — 5/5 + 42/42 + 11/11 pass, full ctest 32/32 + `--smoke` OK.

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
| **R5 — Settings & onboarding ✅** *landed 2026-09-23* | Settings search + reset + profiles + responsive dialog; settings JSON portability; per-page reset; profiles; first-run tour; cheatsheet; Settings crash hardening and crash-report toolkit | **U14** ✅, **U15** ✅, **U19** ✅, **U20** ✅ |
| **R6 — Platform polish ✅** *landed 2026-09-23* | Portal-paste consent, screencast blur + indicator, KRunner categories, tray UI refresh | **U18** ✅, §8 |
| **Later** | Semantic search UI, LAN-sync pairing UI, browser companion, stats dashboard, CopyQ `.cpq` reader, `.zip` backups | Carried long-term; each needs its own UI pass against this system |

> R0–R2 are sequential (shell before surfaces). R3–R6 can reorder by need — each ships independently. **U10/U11/U12/U13/U14/U15/U19 (P0), U17/U18/U20 (P1) are delivered**; R4 ✅, R5 ✅; remaining: §8/R6 platform tails (portal paste, screencast, KRunner preview), then P1 docs/verification.

---

## 11. Non-goals

- No QML/Electron rewrite — Qt Widgets + KF6 stays.
- No cloud, no account, no telemetry, no AI exfiltration.
- No bundled themes/icons — runtime-discovered, read-only.
- No vacuum/DB work on the GUI thread; no hardcoded paths (use `QStandardPaths` + `SettingsManager`).

---

*Last updated: 2026-09-23 (status synced: U1–U20 delivered; R4 ✅, R5 ✅, R6 ✅; P0 complete; P1 ✅ — docs, release checklist, back-compat, privacy review, packaging/platform coverage; full ctest 34/34 + `--smoke` OK) · P2 proposals remain scoped-only by design — stopping here.*
