# Egoboard — Future Plan

> Local-first clipboard history for KDE Plasma. This roadmap balances what's already solid (SQLite + WAL history, X11/Wayland tracking, virtualized two-pane UI, groups & pins) with modern features worth adding next — without turning the app into a cloud service.

**Status:** `v0.1.0` · Qt 6 + KF6 · local-only · MIT · no compilation required for this document. Phases 1–4b, Track E and KRunner are **delivered** — condensed to struck-through single lines below. Active backlog: semantic search (Track A), LAN sync / browser companion (Track D), and the **Settings & QoL backlog (§3)**.

---

## 1. Where we are

Egoboard already covers the fundamentals well: unlimited history with content hashing & dedup, source-app tracking (EWMH on X11 / `wlr-foreign-toplevel` on Wayland), a virtualized list with keyset pagination, favorites & nested groups, `KGlobalAccel` shortcuts (`Meta+V` / `Meta+Shift+V` + `1–9`), and a privacy-aware storage layer (`SensitiveDataDetector`, per-item size cap, `VACUUM` on a worker thread). Recent additions: per-type capture filters, an entry-count retention cap, and a toggleable timeline strip. The architecture is clean — `egoboard_core` stays `QtCore+Sql` only, platform seams go through `IClipboardStorage` / `IActiveWindowTracker`, and `ApplicationContext` is the composition root.

The next steps are not about re-doing that. They're about **understanding content**, **speeding up retrieval**, **automating repetitive paste workflows**, and **polishing daily-driver ergonomics** — all offline.

### Guiding principles for every future feature

1. **Local-first, private by default.** No account, no telemetry, no network call without explicit opt-in.
2. **Performance is a feature.** History must stay instant at 50k+ entries.
3. **No core pollution.** `src/core` stays GUI-free and testable headless.
4. **Wayland parity.** Anything that works on X11 must have a Wayland-respectful equivalent (notification > injection).

---

## 2. Feature tracks (delivered work struck through)

### Track A — Intelligence & Search

* ~~Fuzzy command palette (`Ctrl+K`) and FTS5 full-text search (`unicode61` tokenizer, prefix queries) — delivered (Phase 1).~~
* **Optional local semantic search** (opt-in, ONNX-quantized embedding, e.g. `all-MiniLM-L6-v2` via `onnxruntime`). No network. Embeddings stored alongside entries; cosine search for "that postgres error from last week".

```cpp
// core/SearchEngine.h — new, still Core+Sql only
class SearchEngine : public QObject {
    Q_OBJECT
public:
    // FTS5 path — always available
    QVector<qint64> searchFts(const QString &query, int limit) const;

    // Semantic path — only if user enabled local model
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

---

## 3. Settings & QoL backlog

> Candidate knobs and small delighters, grouped by area. Most items are `SettingsManager` keys (`KConfig`) plus UI wiring; heavier efforts are called out. Nothing here requires a new backend or a network call.

### 3.1 Capture & history

* **Pause capture** — tray menu entry + global shortcut; optional auto-pause while a fullscreen app is active (presentations, games).
* **Noise filter** — skip whitespace-only, single-character or very short copies (`minTextLength`, default 0).
* **Ignore private windows** — never record from incognito/private browser windows.
* **Clipboard ↔ selection sync** — optional X11 mode that mirrors clipboard to primary selection.
* **Auto-backups** — daily JSON export into a configurable folder, keep last N.
* **Move history DB** — override the database path from Settings (with a safe move + reopen flow).

### 3.2 List & reading

* **List density** — compact / comfortable / spacious row heights.
* **Relative timestamps** — "2 h ago" vs absolute; 12/24 h clock setting.
* **Row extras** — optional entry index, use-count badge, "pasted today" dot.
* **Sort modes** — newest first (default) / oldest first / most used.
* **Single-click paste** — optional; double-click stays the default activator.
* **Geometry memory** — remember window size/position and splitter ratio; restore last filter on start.
* **Saved searches ("smart folders")** — pin a filter combination (e.g. "unpinned images from GIMP") to the sidebar.
* **Tags** — user labels per entry, filter chips, palette `>tag` support.
* **Search highlight + recent searches** — mark matched substrings in results; dropdown of previous queries.

### 3.3 Pasting

* **Paste behavior settings** — close window after paste (default on), bump pasted entry to top (default on), global "always paste as plain text".
* **"Paste as" submenu** — plain text, UPPERCASE, with timestamp, image → PNG file next to the cursor.
* **Number-key paste** — `Ctrl+1…9` pastes the first nine visible entries from the main window.
* **Global delete-last hotkey** — drop the most recent capture without opening the window.
* **Quick paste 2.0** — search-as-you-type inside the popup, optional two-line previews, multi-monitor placement memory. *(bigger effort)*

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

## 4. Phased delivery

Keep it iterative. Each phase ships and is usable on its own — no big-bang rewrite.

| Phase | Focus | Key deliverables |
|-------|-------|------------------|
| **Phase 1 — Foundation** ✅ | Search + UX speed | FTS5 migration, fuzzy command palette (`Ctrl+K`), code-aware preview, timeline strip |
| **Phase 2 — Understanding** ✅ | Content smarts | OCR worker (Tesseract), link/color previews, per-app ignore rules |
| **Phase 3 — Automation** ✅ | Daily-driver loop | Transform actions (QuickJS sandbox), snippet templates, KRunner plugin |
| **Phase 4 — Platform** ✅ | Wayland & capture | Layer-shell popup, optional `wlr-data-control` helper |
| **Phase 5 — Ergonomics** | Settings & QoL | §3 backlog in batches: paste behavior + list density first, then saved searches & tags, then Quick paste 2.0 |
| **Tracks A / D** | Long-term | Semantic search (Track A), LAN sync + browser companion (Track D) |

> Phases are sequential by default, but §3 batches can be reordered freely by need — every batch ships independently.

---

## 5. Architecture notes (staying clean)

* New modules respect the existing boundary: `SearchEngine` and `TransformEngine` would live in `egoboard_core` (or a new `egoboard_transform` static lib) and stay headless-testable. OCR, layer-shell, and networking stay in `src/app`.
* Migrations are additive: FTS5 table + triggers, new `entry_embeddings` table (nullable), new `actions/` directory — no breaking change to `history.db`.
* All heavy work off the GUI thread: `QThreadPool` for OCR, `QtConcurrent` for embeddings, `VacuumWorker` pattern for compaction.
* Feature flags in `SettingsManager` (`KConfig` groups) so users can disable OCR / semantic search / sync independently.

---

## 6. Non-goals (intentionally not doing)

* No cloud account, no central server, no telemetry.
* No AI that sends clipboard content to an external API.
* No Electron / rewrite — Qt Widgets + KF6 stays.
* No automatic history upload; sync is pairwise + E2E if ever enabled.

---

## 7. Working locally (as requested)

This roadmap is a **document only** — no build, no CI, no compiled artifact. To iterate on it locally:

```bash
# edit this file, preview in any markdown viewer
xdg-open ROADMAP.md

# or just keep it in the repo root for visibility
git log --oneline -- ROADMAP.md
```

Feedback loop: check one batch, implement, run `cmake --build build && ctest --test-dir build && ./build/egoboard --smoke`, then move to the next. The plan stays in version control so it evolves with the code.

---

*Last updated: 2026-09-08 · Maintainer: local development · Next review: after the first Phase 5 batch — remaining: semantic search (Track A), browser/LAN sync (Track D).*
