<p>
  <img src="data/egoboard.svg" width="128" alt="Egoboard icon">
</p>

# Egoboard

<p>
  <a href="https://github.com/alptekinege/egoboard"><img src="https://img.shields.io/github/stars/alptekinege/egoboard?style=flat-square" alt="GitHub stars"></a>
  <img src="https://img.shields.io/badge/license-MIT-green?style=flat-square" alt="License: MIT">
  <img src="https://img.shields.io/badge/Qt-6-blue?style=flat-square" alt="Qt 6">
  <img src="https://img.shields.io/badge/C++-20-blue?style=flat-square" alt="C++20">
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20KDE%20Plasma-blue?style=flat-square" alt="Platform: Linux, KDE Plasma">
</p>

Clipboard history manager for KDE Plasma. Keeps everything you copy, lets you search it, and paste it back with one keypress. Works on X11 and Wayland. Local-only: SQLite database, no accounts, no network, no telemetry.

## Features

- **History** — text, HTML, images, and file copies in a deduplicated SQLite store; OCR text for images when `tesseract` is installed; keyset paging keeps 50k+ entries instant.
- **Search & filter** — full-text search with quoted phrases, `OR`/`AND`/`NOT`, regex, and field filters (`app:`, `type:`, `tag:`, `pinned:`, `sensitive:`, `has:ocr`, `before:`, `after:`), plus saved/recent searches, a 14-day timeline, and removable filter chips.
- **Organize & paste** — pins, tags, nested groups with drag & drop, and a keyboard-driven quick-paste popup (`Meta+V`) with paste-as variants.
- **Automate** — snippets with placeholders, 17 built-in transforms and chained transforms, and sandboxed JavaScript actions; reach them from the `Ctrl+K` command palette.
- **Privacy** — auto-detects credit cards, passwords, and API keys; flag, exclude, or redact them; per-app ignore lists; previews blur for sensitive entries and while the screen is shared; opt-in SQLCipher encryption with the key in KWallet.
- **Auto-cleanup** — rule-based expiry by age and type plus a disk quota, with trash-based undo for deletes and sweeps.
- **Backups** — single-file `.zip` backups with the database, settings, and an integrity manifest, or an atomic multi-file fallback when libzip is unavailable; restore validates first and offers merge or overwrite; optional daily backups with retention.
- **Export & import** — JSON, Markdown, CSV, HTML, and image folders with a manifest; imports from JSON, Klipper, and unencrypted CopyQ `.cpq` exports (tab preview, content-hash dedup, cancelable rollback).
- **Insights & integration** — a local usage dashboard, KRunner `eb` trigger, D-Bus API, and a tray menu with wheel cycling; no accounts, network, or telemetry.
- **Themes** — follows your active Plasma color scheme and icon theme, or pick any installed KDE scheme; text size and colors are adjustable when a theme is hard to read.

## Docs

- [Usage](./docs/usage.md) — shortcuts, search syntax, privacy, backups/imports, dashboard, KRunner/D-Bus, platform notes, diagnostics.
- [Build](./docs/build.md) — dependencies, optional integrations, build/test commands, AppImage.
- [Release checklist](./docs/release-checklist.md) — X11/Wayland, optional-feature matrix, packaging.

## License

MIT.
