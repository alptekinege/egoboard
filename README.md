<p align="center">
  <img src="data/egoboard.svg" width="128" alt="Egoboard icon">
</p>

# Egoboard

Clipboard history manager for KDE Plasma. Keeps everything you copy, lets you search it, and paste it back with one keypress. Works on X11 and Wayland.

## Features

- **History** — stores text, HTML, images, and file copies in SQLite.
- **Search & filter** — filter by type, date, and source app.
- **Pins & groups** — pin favorites, organize them into nested groups.
- **Shortcuts** — `Meta+V` toggles the window, `Meta+Shift+V` opens quick-paste.
- **Privacy** — auto-detects credit cards, passwords, and API keys; flag, exclude, or redact them.
- **Auto-cleanup** — rule-based expiry by age and type, plus disk quota.
- **Export/Import** — backup and restore as JSON.

## Build

```bash
sudo pacman -S --needed base-devel cmake ninja gcc \
    qt6-base qt6-tools kf6-kconfig kf6-kglobalaccel kf6-knotifications \
    kstatusnotifieritem kf6-kwindowsystem kf6-kxmlgui extra-cmake-modules \
    wayland sqlite

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

For an AppImage:

```bash
./scripts/build-appimage.sh
```

## Run

```bash
./build/egoboard
```

The app lives in the system tray. Settings are in `~/.config/egoboardrc`, the database in `~/.local/share/egoboard/history.db`.

## License

MIT.
