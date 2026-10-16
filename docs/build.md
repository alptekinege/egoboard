# Build

Build Egoboard from source on a Linux system with Qt 6 and KDE Frameworks 6.
The CMake project requires CMake 3.24+, a C++20 compiler, Qt 6.8+, and the
listed KF6 modules. Use a distro release whose repositories provide those
versions; older releases may need newer Qt/KF6 packages from an official
backports repository.

## Required build dependencies

- CMake 3.24 or newer, Ninja, and a C++20 compiler.
- Qt 6.8 or newer: Core, Gui, Widgets, Sql, DBus, Concurrent, Network,
  WaylandClient, and Qml. The Qt Test component is also needed when building
  tests (the default).
- KDE Frameworks 6: Config, ConfigWidgets, WidgetsAddons, GlobalAccel,
  Notifications, StatusNotifierItem, WindowSystem, and XmlGui.
- Extra CMake Modules (ECM), `pkg-config`, and the Wayland client development
  files plus `wayland-scanner`. Egoboard's Wayland protocol XML files are
  vendored in the repository; `wayland-protocols` is not needed to generate
  them.

## Install packages

These commands install the required dependencies on three common distributions.
They include development packages needed by CMake, not just runtime libraries.

### Arch Linux and Arch-based distributions

```bash
sudo pacman -S --needed base-devel cmake ninja pkgconf \
    qt6-base qt6-declarative qt6-wayland \
    wayland extra-cmake-modules \
    kconfig kconfigwidgets kwidgetsaddons kglobalaccel knotifications \
    kstatusnotifieritem kwindowsystem kxmlgui
```

See the [Arch package database](https://archlinux.org/packages/) for the
current repository packages.

### Fedora

```bash
sudo dnf install @development-tools cmake ninja-build pkgconf-pkg-config \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtwayland-devel \
    wayland-devel extra-cmake-modules \
    kf6-kconfig-devel kf6-kconfigwidgets-devel kf6-kwidgetsaddons-devel \
    kf6-kglobalaccel-devel kf6-knotifications-devel \
    kf6-kstatusnotifieritem-devel kf6-kwindowsystem-devel kf6-kxmlgui-devel
```

Package names follow Fedora's [Qt 6 package index](https://packages.fedoraproject.org/pkgs/qt6-qtbase/)
and [KF6 development package index](https://packages.fedoraproject.org/pkgs/kf6-kconfig/kf6-kconfig-devel/).

### Debian 13 (Trixie) and newer

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config \
    qt6-base-dev qt6-declarative-dev qt6-wayland-dev \
    libwayland-dev extra-cmake-modules \
    libkf6config-dev libkf6configwidgets-dev libkf6widgetsaddons-dev \
    libkf6globalaccel-dev libkf6notifications-dev \
    libkf6statusnotifieritem-dev libkf6windowsystem-dev libkf6xmlgui-dev
```

Debian 13 provides Qt 6.8 and KF6 development packages; check the
[Debian package index](https://packages.debian.org/trixie/) if you use a
different release or architecture. Ubuntu users should check that their
enabled repositories provide Qt 6.8 or newer and all listed KF6 development
packages before building; older LTS repositories may not meet the minimum.

## Optional integrations

These packages are not needed for a basic build. CMake enables the related
feature automatically when its development package is present:

- `libxtst` — XTest auto-paste on X11 (`libxtst` on Arch, `libxtst-dev` on
  Debian, `libXtst-devel` on Fedora).
- `layer-shell-qt` — Wayland quick-paste layer-shell support
  (`layer-shell-qt` on Arch, `liblayershellqtinterface-dev` on Debian,
  `layer-shell-qt-devel` on Fedora).
- `pipewire` / `libpipewire-0.3` — screencast detection and preview auto-blur
  (`pipewire` on Arch, `libpipewire-0.3-dev` on Debian, `pipewire-devel` on
  Fedora).
- `libzip` — single-file ZIP backups (`libzip` on Arch, `libzip-dev` on Debian,
  `libzip-devel` on Fedora). SQLite development files (`sqlite` on Arch,
  `libsqlite3-dev` on Debian, `sqlite-devel` on Fedora) must also be available.
- `kwallet` and `sqlcipher` — database encryption. The CMake option
  `EGOBOARD_USE_SQLCIPHER` defaults to ON and enables encryption only when both
  are found (`kwallet` and `sqlcipher` on Arch; `libkf6wallet-dev` and
  `libsqlcipher-dev` on Debian; `kf6-kwallet-devel` and `sqlcipher-devel` on
  Fedora).
- `krunner` — optional KRunner plugin, built when KF6Runner is found
  (`krunner` on Arch, `libkf6runner-dev` on Debian,
  `kf6-krunner-devel` on Fedora).

At runtime, install `tesseract` with matching language data for OCR and
`xdotool` for the X11 paste fallback. These are not build dependencies. Portal
paste requires a portal-providing Wayland compositor and is enabled in settings.

## Build

The repository helper uses Ninja and Release mode by default:

```bash
./scripts/build.sh
```

Set `EGOBOARD_BUILD_DIR` or `EGOBOARD_BUILD_TYPE` to override the build
directory or CMake configuration. To configure manually:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Tests and diagnostics

Run the complete local verification sequence with:

```bash
./scripts/test.sh
```

The helper builds, runs CTest with `QT_QPA_PLATFORM=offscreen`, and runs the
headless smoke check. If running the suite manually:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/egoboard --smoke
```

`BUILD_TESTING` is ON by default. Pass `-DBUILD_TESTING=OFF` for a build without
the test executables. Tests that use local sockets need AF_UNIX access; run the
suite outside containers or sandboxes that block local sockets.

`--smoke` checks storage, groups, search, export/import, transforms, snippets,
and platform probes, then exits. `--bench[=N]` (default 50,000 entries) loads a
scratch database and reports insertion, page, keyset paging, MostUsed sort, FTS,
and JSON export timings against budgets. The benchmark removes and recreates
`/tmp/egoboard-bench` and `/tmp/egoboard-bench-export.json`.

`-DEGOBOARD_USE_SQLCIPHER=OFF` disables SQLCipher even when its dependencies
are installed. To disable ZIP backups, use `-DEGOBOARD_ENABLE_ZIP_BACKUPS=OFF`.

## AppImage

```bash
./scripts/build-appimage.sh
./scripts/validate-appimage.sh
```

AppImage packaging also needs `appimagetool`, `mksquashfs` (usually from
`squashfs-tools`), Python 3, `binutils` for `readelf` during validation, and
`curl` if the script must download appimagetool.
The packaging script installs a Release build into an AppDir, bundles Qt/KF6
plugins and shared libraries, then creates the image under `dist/`. It removes
and recreates its AppDir staging directory (`build-appimage/AppDir`) on each
run. Set `EGOBOARD_APPIMAGE_BUILD_DIR`, `EGOBOARD_APPIMAGE_APPDIR`, or
`EGOBOARD_APPIMAGE_OUTPUT_DIR` to move those generated files elsewhere.

The first online build may download appimagetool and an AppImage Type-2 runtime.
For an offline build, make appimagetool available at
`packaging/appimage/tools/appimagetool` or in `PATH`, and set
`EGOBOARD_APPIMAGE_RUNTIME_FILE` to a local Type-2 runtime file.
