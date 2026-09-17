# Build

Instructions for building Egoboard from source on Arch Linux.

```bash
sudo pacman -S --needed base-devel cmake ninja gcc \
    qt6-base qt6-tools kf6-kconfig kf6-kconfigwidgets kf6-kwidgetsaddons kf6-kglobalaccel kf6-knotifications \
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
