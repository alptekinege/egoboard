# Build

Instructions for building Egoboard from source. Package names are Arch Linux's;
adjust them for your distribution.

## Dependencies

```bash
sudo pacman -S --needed base-devel cmake ninja gcc \
    qt6-base qt6-declarative \
    wayland wayland-protocols \
    extra-cmake-modules \
    kconfig kconfigwidgets kwidgetsaddons kglobalaccel knotifications \
    kstatusnotifieritem kwindowsystem kxmlgui \
    sqlite
```

Notes:

- `qt6-declarative` is required — the sandboxed JS transform engine uses
  `QJSEngine` (Qt6 Qml). On distributions that ship the Qt Wayland client
  separately, install `qt6-wayland` as well (`Qt6WaylandClient` is what the
  compositor integration links against).
- `extra-cmake-modules` provides `FindWaylandScanner`, used to generate the
  bindings for the vendored `wlr-foreign-toplevel-management` and
  `wlr-data-control` protocol XMLs in `third_party/protocols/`.
- Runtime dependencies for optional features are detected at runtime, not build
  time: `tesseract` (+ language data) for OCR, `xdotool` as the X11 paste
  fallback.

Optional build/feature dependencies:

- `krunner` — builds the KRunner plugin (`eb ` trigger).
- `kwallet` + `sqlcipher` — required for `-DEGOBOARD_USE_SQLCIPHER=ON`
  (encryption at rest with a KWallet-held key).
- `libxtst` — XTest auto-paste on X11 (compiled in automatically when found).
- `layer-shell-qt` — layer-shell quick-paste popup on Wayland (compiled in
  automatically when found).

## Build and test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/egoboard --smoke
```

- `BUILD_TESTING` is ON by default; pass `-DBUILD_TESTING=OFF` to skip the
  test suite. Tests are headless (`QTEST_GUILESS_MAIN` + `QTemporaryDir`).
- `--smoke` runs a headless end-to-end self-check (storage, groups, search,
  export/import, transforms, snippets, platform probes) and exits non-zero on
  failure; it needs no display.
- `--bench[=N]` (default 50000 entries) bulk-loads a scratch database and
  reports insertion, first page, deep keyset paging, most-used sort, FTS and
  JSON export timings against budgets; it exits non-zero when a budget is
  exceeded.
- `-DEGOBOARD_USE_SQLCIPHER=ON` enables the SQLCipher build when `sqlcipher`
  and `kwallet` are available.

## AppImage

```bash
./scripts/build-appimage.sh
```
