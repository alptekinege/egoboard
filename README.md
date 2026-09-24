<p align="center">
  <img src="data/egoboard.svg" width="128" alt="Egoboard icon">
</p>

# Egoboard

Clipboard history manager for KDE Plasma. Keeps everything you copy, lets you search it, and paste it back with one keypress. Works on X11 and Wayland. Local-only: SQLite database, no accounts, no network, no telemetry.

## Features

- **History** — stores text, HTML, images, and file copies in SQLite.
- **Search & filter** — filter by type, date, and source app.
- **Pins & groups** — pin favorites, organize them into nested groups.
- **Shortcuts** — `Meta+V` opens quick-paste, `Meta+Shift+V` toggles the window.
- **Privacy** — auto-detects credit cards, passwords, and API keys; flag, exclude, or redact them.
- **Auto-cleanup** — rule-based expiry by age and type, plus disk quota.
- **Export/Import** — backup and restore as JSON.
- **Themes** — follows your active Plasma color scheme and icon theme, or pick any installed KDE scheme; text size and colors are adjustable when a theme is hard to read.

## Docs

- [Usage](./docs/usage.md) — shortcuts, search syntax, privacy, backups, KRunner/D-Bus, platform notes, diagnostics.
- [Build](./docs/build.md) — dependencies, build/test commands, AppImage.
- [Release checklist](./docs/release-checklist.md)

## License

MIT.
