# Egoboard

A native clipboard history manager for **KDE Plasma**, built with **Qt 6 Widgets
and KDE Frameworks 6**. Works on both **X11 and Plasma Wayland** sessions.

## Features

- **Unlimited history** — every copy (text, rich text/HTML, images, file paths)
  is stored in an SQLite database with timestamp, content type, source app and
  a content hash. No automatic deletion; identical copies just move to the top.
- **Source app tracking** — on X11 via EWMH (`_NET_ACTIVE_WINDOW`, `_NET_WM_PID`
  → process name); on Wayland via the `wlr-foreign-toplevel-management` protocol
  (supported by KWin), which reports the `app_id` and title of the active toplevel.
- **Two-pane UI** — virtualized list (infinite scroll, tested with tens of
  thousands of entries) + preview pane (text/HTML/image/file list), live search
  and filters by type, date range and source app.
- **Favorites & groups** — pin entries, organize them in arbitrarily nested,
  nameable groups with custom colors and icons; drag entries onto groups and
  drag groups onto each other to re-parent.
- **Global shortcuts** via KGlobalAccel: `Meta+V` toggles the window,
  `Meta+Shift+V` opens the quick-paste popup with numeric `1–9` shortcuts.
- **Paste-back** — the entry is restored to the clipboard; on X11 `Ctrl+V` is
  simulated via XTest (or `xdotool`). On Wayland, where key injection is not
  permitted for regular clients, a notification reminds you to press `Ctrl+V`.
- **Privacy** — automatic detection of credit card numbers (Luhn-validated),
  passwords/secrets and API tokens; such content can be marked, excluded, or
  **redacted** (stored as `••••`, per-kind toggles, the original never touches
  disk). The **Audit view** filters flagged entries for bulk review & deletion.
- **Auto-expire rules** — "delete unpinned Terminal copies after 24h":
  rule-based retention by age, content type and source app, applied at startup
  and on a 15-minute schedule (pinned entries survive by default).
- **Export/Import** — the whole database (or selected groups) as JSON, with
  merge / overwrite / skip-duplicate conflict resolution when importing on
  another machine.
- **Storage hygiene** — optional disk-size cap (oldest non-pinned entries are
  dropped), per-entry size limit, background `VACUUM` compaction on a worker
  thread (startup check when >50 MB, then daily, plus a manual button).
- **Autostart** — toggle in settings writes the standard
  `~/.config/autostart/org.egoboard.Egoboard.desktop` entry.
- **Single instance** — a second launch just raises the running one.

## Building

Dependencies (Arch/Arch-derivative package names):

    sudo pacman -S --needed base-devel cmake ninja gcc \
        qt6-base qt6-tools kf6-kconfig kf6-kglobalaccel kf6-knotifications \
        kstatusnotifieritem kf6-kwindowsystem kf6-kxmlgui extra-cmake-modules \
        wayland sqlite

Optional: `libxtst` (auto-paste on X11 without xdotool), `xdotool`, `sqlcipher` + `kwallet` (encrypted DB at rest, `cmake -DEGOBOARD_USE_SQLCIPHER=ON`), `krunner` (KRunner `eb <query>` → clipboard history).

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build          # unit tests
    ./build/egoboard --smoke        # headless end-to-end self-check
    sudo cmake --install build      # optional

## AppImage Packaging

Egoboard provides automated scripts to bundle dependencies (Qt 6, KF6, SQLite, Wayland/XCB plugins) and generate a standalone AppImage:

```bash
# Build the AppImage (saved to dist/Egoboard-<version>-x86_64.AppImage)
./scripts/build-appimage.sh

# Validate AppImage structure, bundled plugins, and run smoke tests
./scripts/validate-appimage.sh
```

## Running

    ./build/egoboard


The app starts into the system tray (StatusNotifierItem). Enable autostart in
Settings → General. Configuration lives in `~/.config/egoboardrc`, the database
in `~/.local/share/egoboard/history.db`.

## Wayland notes

- Active-window detection uses `zwlr_foreign_toplevel_manager_v1`; the
  privileged `org_kde_plasma_window_management` protocol is deliberately not
  used because KWin only allows plasma-shell to bind it.
- The protocol exposes the window's `app_id` (e.g. `org.kde.kate`), not a PID;
  on X11 the real process name is recorded.
- Simulating keystrokes from a regular Wayland client is forbidden, so
  egoboard re-copies the entry and shows a "press Ctrl+V" notification instead
  of auto-pasting.
- The quick-paste popup positions itself on the screen under the cursor; exact
  absolute positioning of popups is not possible without layer-shell.

## Architecture

    src/core     egoboard_core (Qt Core+Sql only — unit-testable storage,
                 bookmarks, export/import, sensitive-data detector, models)
    src/app      GUI application: watcher, trackers (X11/Wayland), tray,
                 hotkeys, auto-paster, settings (KConfig), UI
    third_party  vendored wlr-foreign-toplevel-management protocol XML

Dependencies between components are abstracted behind small interfaces
(`IClipboardStorage`, `IActiveWindowTracker`) and wired in `ApplicationContext`,
the composition root.

## License

MIT.
