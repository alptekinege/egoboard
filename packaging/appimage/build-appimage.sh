#!/usr/bin/env bash
# ==============================================================================
# Egoboard AppImage Packaging Script
# Builds, bundles dependencies, and packages Egoboard into a standalone AppImage.
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUILD_DIR="${ROOT_DIR}/build-appimage"
APPDIR="${BUILD_DIR}/AppDir"
OUTPUT_DIR="${ROOT_DIR}/dist"
TOOLS_DIR="${ROOT_DIR}/packaging/appimage/tools"

VERSION="0.1.0"
ARCH="$(uname -m)"
OUTPUT_APPIMAGE="${OUTPUT_DIR}/Egoboard-${VERSION}-${ARCH}.AppImage"

# ANSI colors for terminal output
BOLD="\033[1m"
GREEN="\033[32m"
YELLOW="\033[33m"
BLUE="\033[34m"
RED="\033[31m"
RESET="\033[0m"

log_info() {
    echo -e "${BLUE}${BOLD}[INFO]${RESET} $1"
}

log_success() {
    echo -e "${GREEN}${BOLD}[SUCCESS]${RESET} $1"
}

log_warn() {
    echo -e "${YELLOW}${BOLD}[WARN]${RESET} $1"
}

log_error() {
    echo -e "${RED}${BOLD}[ERROR]${RESET} $1" >&2
}

# ------------------------------------------------------------------------------
# 1. Environment & Prerequisite Checks
# ------------------------------------------------------------------------------
log_info "Starting Egoboard AppImage packaging for architecture ${BOLD}${ARCH}${RESET}..."

mkdir -p "${TOOLS_DIR}" "${OUTPUT_DIR}"

APPIMAGETOOL="${TOOLS_DIR}/appimagetool"
if command -v appimagetool >/dev/null 2>&1; then
    APPIMAGETOOL="$(command -v appimagetool)"
elif [ ! -f "${APPIMAGETOOL}" ]; then
    log_info "Downloading appimagetool to ${TOOLS_DIR}..."
    curl -fsSL -o "${APPIMAGETOOL}" \
        "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ARCH}.AppImage"
    chmod +x "${APPIMAGETOOL}"
fi

# ------------------------------------------------------------------------------
# 2. Build & Install into AppDir
# ------------------------------------------------------------------------------
log_info "Configuring and compiling Egoboard with CMake..."
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}"

cmake -B "${BUILD_DIR}" -S "${ROOT_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF

cmake --build "${BUILD_DIR}" --target egoboard

log_info "Installing to AppDir staging area..."
DESTDIR="${APPDIR}" cmake --install "${BUILD_DIR}"

# ------------------------------------------------------------------------------
# 3. Deploy Desktop File, Icons, and AppRun
# ------------------------------------------------------------------------------
log_info "Setting up AppDir metadata and icons..."

# Root-level desktop file and icons required by AppImage standard
cp "${ROOT_DIR}/data/org.egoboard.Egoboard.desktop" "${APPDIR}/org.egoboard.Egoboard.desktop"
cp "${ROOT_DIR}/data/egoboard.svg" "${APPDIR}/egoboard.svg"
ln -sf egoboard.svg "${APPDIR}/.DirIcon"

# Ensure scalable icon directory is populated
mkdir -p "${APPDIR}/usr/share/icons/hicolor/scalable/apps"
cp "${ROOT_DIR}/data/egoboard.svg" "${APPDIR}/usr/share/icons/hicolor/scalable/apps/egoboard.svg"

# Create AppRun entry point
cat <<'EOF' > "${APPDIR}/AppRun"
#!/bin/sh
set -e

# Resolve AppDir location
SELF=$(readlink -f "$0")
APPDIR=$(dirname "$SELF")
export APPDIR

# Configure binary search paths
export PATH="${APPDIR}/usr/bin:${PATH}"

# Configure dynamic linker search paths
export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${APPDIR}/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# Configure Qt plugin search path
export QT_PLUGIN_PATH="${APPDIR}/usr/plugins:${APPDIR}/usr/lib/qt6/plugins:${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"

# Configure QML import path
export QML2_IMPORT_PATH="${APPDIR}/usr/qml:${APPDIR}/usr/lib/qt6/qml:${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"

# Configure XDG data and config directories
export XDG_DATA_DIRS="${APPDIR}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export XDG_CONFIG_DIRS="${APPDIR}/etc/xdg:${XDG_CONFIG_DIRS:-/etc/xdg}"

# Execute application
exec "${APPDIR}/usr/bin/egoboard" "$@"
EOF
chmod +x "${APPDIR}/AppRun"

# ------------------------------------------------------------------------------
# 4. Deploy Essential Qt 6 & KF6 Plugins
# ------------------------------------------------------------------------------
log_info "Deploying targeted Qt 6 & KF6 plugins..."

QT_PLUGIN_SRC="$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || echo "/usr/lib/qt6/plugins")"
QT_DEST_PLUGINS="${APPDIR}/usr/plugins"
mkdir -p "${QT_DEST_PLUGINS}"

# Specific plugin directories required by Egoboard
copy_plugin_dir() {
    local sub="$1"
    if [ -d "${QT_PLUGIN_SRC}/${sub}" ]; then
        mkdir -p "${QT_DEST_PLUGINS}/${sub}"
        cp -a "${QT_PLUGIN_SRC}/${sub}"/* "${QT_DEST_PLUGINS}/${sub}/" 2>/dev/null || true
    fi
}

copy_plugin_dir "platforms"
copy_plugin_dir "sqldrivers"
copy_plugin_dir "wayland-shell-integration"
copy_plugin_dir "wayland-decoration-client"
copy_plugin_dir "wayland-graphics-integration-client"
copy_plugin_dir "iconengines"
copy_plugin_dir "platformthemes"

# Targeted image formats (SVG, JPEG, ICO, WEBP, GIF)
if [ -d "${QT_PLUGIN_SRC}/imageformats" ]; then
    mkdir -p "${QT_DEST_PLUGINS}/imageformats"
    for fmt in libqsvg.so libqjpeg.so libqico.so libqwebp.so libqgif.so; do
        if [ -f "${QT_PLUGIN_SRC}/imageformats/${fmt}" ]; then
            cp -a "${QT_PLUGIN_SRC}/imageformats/${fmt}" "${QT_DEST_PLUGINS}/imageformats/"
        fi
    done
fi

# KF6 specific plugins if present
for kf_sub in "kf6/kwindowsystem" "kf6/knotifications6" "kf6/org.kde.kglobalacceld.platforms"; do
    copy_plugin_dir "${kf_sub}"
done

# ------------------------------------------------------------------------------
# 5. Dependency Collection (Fast BFS Dependency Bundling)
# ------------------------------------------------------------------------------
log_info "Collecting shared library dependencies..."

mkdir -p "${APPDIR}/usr/lib"

python3 - <<PYEOF
import os
import re
import subprocess
import shutil

appdir = "${APPDIR}"
lib_dir = os.path.join(appdir, "usr", "lib")
os.makedirs(lib_dir, exist_ok=True)

# System library exclusion patterns (host glibc / kernel ABI)
exclude_patterns = [
    r"^libc\.so",
    r"^libm\.so",
    r"^libdl\.so",
    r"^libpthread\.so",
    r"^librt\.so",
    r"^ld-linux",
    r"^libresolv\.so",
    r"^libgcc_s\.so",
]
exclude_regex = re.compile("|".join(exclude_patterns))

def is_excluded(name):
    return bool(exclude_regex.search(name))

def get_dependencies(filepath):
    try:
        res = subprocess.run(["ldd", filepath], capture_output=True, text=True, check=True)
    except Exception:
        return []
    deps = []
    for line in res.stdout.splitlines():
        line = line.strip()
        if "=>" in line:
            parts = line.split("=>")
            if len(parts) > 1:
                target = parts[1].strip().split()[0]
                if target.startswith("/"):
                    deps.append(target)
    return deps

# Initial queue: main binary and all deployed plugins
initial_files = []
bin_path = os.path.join(appdir, "usr", "bin", "egoboard")
if os.path.exists(bin_path):
    initial_files.append(bin_path)

plugins_dir = os.path.join(appdir, "usr", "plugins")
for root, _, files in os.walk(plugins_dir):
    for f in files:
        if f.endswith(".so") or ".so." in f:
            initial_files.append(os.path.join(root, f))

visited_libs = set()
queue = list(initial_files)

while queue:
    current = queue.pop(0)
    deps = get_dependencies(current)
    for dep in deps:
        libname = os.path.basename(dep)
        if is_excluded(libname):
            continue
        dest = os.path.join(lib_dir, libname)
        if not os.path.exists(dest):
            try:
                shutil.copy2(dep, dest)
                visited_libs.add(libname)
                queue.append(dest)
            except Exception as e:
                print(f"Warning: Failed to copy {dep}: {e}")

print(f"Successfully bundled {len(visited_libs)} shared libraries into AppDir.")
PYEOF

# ------------------------------------------------------------------------------
# 6. Deploy KDE / Freedesktop Resources
# ------------------------------------------------------------------------------
log_info "Bundling desktop notifications and icon data..."

if [ -d "/usr/share/knotifications6" ]; then
    mkdir -p "${APPDIR}/usr/share/knotifications6"
    cp -a /usr/share/knotifications6/* "${APPDIR}/usr/share/knotifications6/" 2>/dev/null || true
fi

# ------------------------------------------------------------------------------
# 7. Assemble AppImage
# ------------------------------------------------------------------------------
log_info "Assembling AppImage with appimagetool..."

export ARCH
export APPIMAGE_EXTRACT_AND_RUN=1

"${APPIMAGETOOL}" --no-appstream "${APPDIR}" "${OUTPUT_APPIMAGE}"

chmod +x "${OUTPUT_APPIMAGE}"

# Create convenient symlink: Egoboard-x86_64.AppImage
ln -sf "$(basename "${OUTPUT_APPIMAGE}")" "${OUTPUT_DIR}/Egoboard-${ARCH}.AppImage"

log_success "AppImage created successfully at:"
log_success "  ${OUTPUT_APPIMAGE}"
log_success "  ${OUTPUT_DIR}/Egoboard-${ARCH}.AppImage"
