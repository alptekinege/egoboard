# Egoboard — Future Plan

> Local-first clipboard history for KDE Plasma. This roadmap balances what's already solid (SQLite + WAL history, X11/Wayland tracking, virtualized two-pane UI, groups & pins) with modern features worth adding next — without turning the app into a cloud service.

**Status:** `v0.1.0` · Qt 6 + KF6 · local-only · MIT · no compilation required for this document.

---

## 1. Where we are

Egoboard already covers the fundamentals well: unlimited history with content hashing & dedup, source-app tracking (EWMH on X11 / `wlr-foreign-toplevel` on Wayland), a virtualized list with keyset pagination, favorites & nested groups, `KGlobalAccel` shortcuts (`Meta+V` / `Meta+Shift+V` + `1–9`), and a privacy-aware storage layer (`SensitiveDataDetector`, per-item size cap, `VACUUM` on a worker thread). The architecture is clean — `egoboard_core` stays `QtCore+Sql` only, platform seams go through `IClipboardStorage` / `IActiveWindowTracker`, and `ApplicationContext` is the composition root.

The next steps are not about re-doing that. They're about **understanding content**, **speeding up retrieval**, and **automating repetitive paste workflows** — all offline.

### Guiding principles for every future feature

1. **Local-first, private by default.** No account, no telemetry, no network call without explicit opt-in.
2. **Performance is a feature.** History must stay instant at 50k+ entries.
3. **No core pollution.** `src/core` stays GUI-free and testable headless.
4. **Wayland parity.** Anything that works on X11 must have a Wayland-respectful equivalent (notification > injection).

---

## 2. Modern feature tracks

### Track A — Intelligence & Search

Current search is substring + filters. Modern expectation is instant, typo-tolerant, and semantic.

* **Fuzzy finder + command palette** (`Ctrl+K`): quick-paste, pin, move-to-group, delete, transform — all from one palette.
* **Full-text search with FTS5**: migrate `preview`/`textData` into an `fts5` virtual table, keep it in sync via triggers. Sub-millisecond search on large DBs, with highlighting.
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

The watcher already classifies `Text / RichText / Image / Files`. The next layer is *understanding* what's inside.

* **Image OCR** (local Tesseract): copied screenshots / slides become searchable text. OCR runs on a `QThreadPool` worker, not the GUI thread.
* **Code-aware previews**: detect language (via extension or heuristics), add syntax highlighting + "copy as plain" in `PreviewPane`.
* **Link & color previews**: inline thumbnails for URLs, color swatches for hex codes, file-icon grid for `Files` type.
* **Smart title generation**: `2024-08-23 14:02 — copied from Kate` → `Fix dedup hash in StorageManager` when the text is a commit message.

```cpp
// app/OcrWorker.h — queued, cancellable
class OcrWorker : public QObject {
    Q_OBJECT
public slots:
    void recognize(qint64 entryId, QImage image);
signals:
    void recognized(qint64 entryId, QString text, double confidence);
};
```

### Track C — Workflow Automation

This is what turns a history viewer into a daily driver (CopyQ / Alfred-style).

* **Transform actions** — user-defined JavaScript (QuickJS, sandboxed) or small built-in transforms: `Trim → JSON pretty → Base64 → URL decode → Uppercase`. Chainable, previewed live.
* **Snippet templates & placeholders**: pin a template like `git commit -m "{{clipboard}}" [{{date}}]` and expand on paste.
* **Per-app rules**: "never record from KeePassXC / 1Password", "auto-uppercase when source is Terminal", "ignore images > 5 MB from GIMP".
* **Scripting hook** (minimal plugin surface):

```js
// ~/.local/share/egoboard/actions/prettify-json.js
export function transform(text) {
  return JSON.stringify(JSON.parse(text), null, 2);
}
export const meta = { label: "Pretty JSON", match: /^\s*\{/ };
```

### Track D — Platform & Integration

* **Wayland layer-shell quick-paste**: migrate `QuickPasteMenu` to `wlr-layer-shell` where available for correct exclusive positioning and keyboard grab.
* **Wayland clipboard via `wlr-data-control`** (optional privileged helper) for more reliable history on compositors that support it.
* **KDE Connect / LAN sync** (opt-in, E2E-encrypted): sync pinned snippets across devices on the same network using a pairing code — never a central server. History sync stays off by default.
* **Browser companion** (optional WebExtension, local WebSocket on `127.0.0.1`): copy code blocks with one click, preserve source URL.

### Track E — Privacy & Security Hardening

* **Redaction mode**: detected secrets are stored as `••••` unless user explicitly reveals; `Exclude` mode already exists — add per-pattern toggles (cards / tokens / private keys).
* **Encrypted at rest** (opt-in): SQLCipher build flag for the DB file; key held in KWallet.
* **Auto-expire rules**: "delete unpinned Terminal copies after 24h", "keep Images for 7 days".
* **Audit view**: filter `sensitive = 1` and bulk-delete.

### Track F — UX Polish

* **Timeline / calendar strip** above the list (like a commit graph) for jumping by day.
* **Inline diff** for dedup: "same hash as 2h ago — updated `use_count`".
* **Global search widget** (Plasma applet / KRunner plugin) so `KRunner → eb <query>` opens directly.
* **Accessibility**: full keyboard navigation, screen-reader labels, high-contrast delegate.

---

## 3. Phased delivery

Keep it iterative. Each phase ships and is usable on its own — no big-bang rewrite.

| Phase | Focus | Key deliverables |
|-------|-------|------------------|
| **Phase 1 — Foundation** (1–2 weeks) | Search + UX speed | FTS5 migration, fuzzy command palette (`Ctrl+K`), code-aware preview, timeline strip |
| **Phase 2 — Understanding** (2–3 weeks) | Content smarts | OCR worker (Tesseract), link/color previews, per-app ignore rules |
| **Phase 3 — Automation** (2–3 weeks) | Daily-driver loop | Transform actions (QuickJS sandbox), snippet templates, KRunner plugin |
| **Phase 4 — Platform** (3–4 weeks) | Wayland & sync | Layer-shell popup, optional `wlr-data-control` helper, LAN E2E sync for pins |

> Phases are sequential by default, but Tracks B and C can be swapped if automation is more valuable to you day-to-day.

---

## 4. Architecture notes (staying clean)

* New modules respect the existing boundary: `SearchEngine` and `TransformEngine` would live in `egoboard_core` (or a new `egoboard_transform` static lib) and stay headless-testable. OCR, layer-shell, and networking stay in `src/app`.
* Migrations are additive: FTS5 table + triggers, new `entry_embeddings` table (nullable), new `actions/` directory — no breaking change to `history.db`.
* All heavy work off the GUI thread: `QThreadPool` for OCR, `QtConcurrent` for embeddings, `VacuumWorker` pattern for compaction.
* Feature flags in `SettingsManager` (`KConfig` group `Features`) so users can disable OCR / semantic search / sync independently.

---

## 5. Non-goals (intentionally not doing)

* No cloud account, no central server, no telemetry.
* No AI that sends clipboard content to an external API.
* No Electron / rewrite — Qt Widgets + KF6 stays.
* No automatic history upload; sync is pairwise + E2E if ever enabled.

---

## 6. Working locally (as requested)

This roadmap is a **document only** — no build, no CI, no compiled artifact. To iterate on it locally:

```bash
# edit this file, preview in any markdown viewer
xdg-open ROADMAP.md

# or just keep it in the repo root for visibility
git log --oneline -- ROADMAP.md
```

Feedback loop: check one phase, implement, run `cmake --build build && ctest --test-dir build && ./build/egoboard --smoke`, then move to the next. The plan stays in version control so it evolves with the code.

---

*Last updated: 2026-08-23 · Maintainer: local development · Next review after Phase 1.*
