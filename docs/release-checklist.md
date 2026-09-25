# Release checklist

Repeatable verification for an Egoboard release, on both supported sessions.
Check each box on real hardware/VMs; compositor-dependent behavior cannot be
proven headless (see the last section).

## 1. Clean build + gates

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/egoboard --smoke
QT_QPA_PLATFORM=offscreen ./build/egoboard --bench=100000  # budgets must hold
```

## 2. Plasma X11 session

- Log in, copy text/HTML/image/files — all four land in history with the
  right source app.
- `Meta+V` popup pastes via XTest (uninstall `xdotool` first to prove the
  primary path); with neither XTest nor `xdotool`, the manual-paste
  notification appears instead.
- Global shortcuts (`Meta+V`, `Meta+Shift+V`, `Meta+Shift+D`,
  `Meta+Shift+P`) fire while the window is hidden.
- Autostart: enable it, log out/in, the app returns (entry points at the
  installed binary, never a bare command name).
- Dashboard (P2-A): open from the toolbar (More ▸ Dashboard off Wide) and via
  `>dashboard` — activity/type/top-apps/size sections render, charts take
  keyboard focus (arrows/Home/End), and an empty profile shows the empty state.

## 3. Plasma Wayland session

- Copy via keyboard and via middle-click (if primary-selection monitoring is
  on); `wlr-data-control` status in Settings ▸ Diagnostics when KWin exposes
  it, QClipboard polling otherwise.
- Paste shows the “press Ctrl+V” notification; with portal paste opted in,
  Plasma prompts once and pasting goes through the portal afterwards.
- Start a screen share (e.g. a video call): the status-bar Sharing indicator
  appears and previews blur until hovered; stopping clears both.
- `eb query` in KRunner lists grouped matches with working actions.
- Dashboard (P2-A): same as on X11 — toolbar/More and `>dashboard` open the
  local aggregates; charts are keyboard-focusable; no entry text is shown.

## 4. Optional-feature matrix

| Build / runtime piece | Absent behavior (must hold) |
|---|---|
| KRunner dev files | Plugin skipped; app + tests unaffected |
| `layer-shell-qt` | Popup falls back to a normal window |
| XTest / `xdotool` | Manual-paste notification on X11 |
| SQLCipher/KWallet | Encryption checkbox explains the missing build; history stays plaintext |
| `libpipewire-0.3` | Sharing never reported; no indicator, no blur change |
| `tesseract` | Images carry no OCR text; preview names the install hint |
| Portal service | Portal paste degrades to the notification; status line says so |
| QtCharts | Not required: the dashboard paints with the built-in QPainter fallback; no behavior change |

## 5. Packaging

- `data/org.egoboard.Egoboard.desktop`: valid desktop entry (covered by
  `tst_packaging`), installed with icon + autostart handling intact.
- AppImage: `./scripts/build-appimage.sh`, then
  `./scripts/validate-appimage.sh` (or `packaging/appimage/validate-appimage.sh`).
- Settings ▸ Diagnostics report on the release build shows sane versions,
  pragmas, and subsystem states.
