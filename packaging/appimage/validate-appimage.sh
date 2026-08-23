#!/usr/bin/env bash
# ==============================================================================
# Egoboard AppImage Validation Script
# Verifies AppImage structure, metadata, bundled plugins, and runs smoke tests.
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

DIST_DIR="${ROOT_DIR}/dist"
ARCH="$(uname -m)"
APPIMAGE_PATH="${1:-${DIST_DIR}/Egoboard-${ARCH}.AppImage}"

# ANSI colors
BOLD="\033[1m"
GREEN="\033[32m"
YELLOW="\033[33m"
BLUE="\033[34m"
RED="\033[31m"
RESET="\033[0m"

log_info() {
    echo -e "${BLUE}${BOLD}[INFO]${RESET} $1"
}

log_pass() {
    echo -e "${GREEN}${BOLD}[PASS]${RESET} $1"
}

log_fail() {
    echo -e "${RED}${BOLD}[FAIL]${RESET} $1" >&2
}

log_warn() {
    echo -e "${YELLOW}${BOLD}[WARN]${RESET} $1"
}

FAILURES=0

assert_true() {
    local desc="$1"
    local condition="$2"
    if eval "${condition}"; then
        log_pass "${desc}"
    else
        log_fail "${desc}"
        FAILURES=$((FAILURES + 1))
    fi
}

echo -e "\n${BOLD}======================================================${RESET}"
echo -e "${BOLD}       Egoboard AppImage Validation Suite             ${RESET}"
echo -e "${BOLD}======================================================${RESET}\n"

# ------------------------------------------------------------------------------
# 1. Existence and Permissions Check
# ------------------------------------------------------------------------------
log_info "Target AppImage: ${APPIMAGE_PATH}"

if [ ! -f "${APPIMAGE_PATH}" ]; then
    log_fail "AppImage file not found at ${APPIMAGE_PATH}."
    log_info "Please run packaging/appimage/build-appimage.sh first."
    exit 1
fi

assert_true "AppImage file exists and is non-empty" "[ -s '${APPIMAGE_PATH}' ]"
assert_true "AppImage file is marked executable" "[ -x '${APPIMAGE_PATH}' ]"

APPIMAGE_SIZE="$(du -h -L "${APPIMAGE_PATH}" | awk '{print $1}')"
APPIMAGE_SHA256="$(sha256sum "$(readlink -f "${APPIMAGE_PATH}")" | awk '{print $1}')"
log_info "File Size: ${APPIMAGE_SIZE}"
log_info "SHA-256:   ${APPIMAGE_SHA256}"

# ------------------------------------------------------------------------------
# 2. Extract and Inspect Metadata
# ------------------------------------------------------------------------------
TEMP_EXTRACT_DIR="$(mktemp -d -t egoboard-appimage-val-XXXXXX)"
cleanup() {
    rm -rf "${TEMP_EXTRACT_DIR}"
}
trap cleanup EXIT

log_info "Extracting AppImage contents for inspection..."
(
    cd "${TEMP_EXTRACT_DIR}"
    export APPIMAGE_EXTRACT_AND_RUN=1
    "${APPIMAGE_PATH}" --appimage-extract >/dev/null 2>&1
)

EXTRACTED_DIR="${TEMP_EXTRACT_DIR}/squashfs-root"

assert_true "Extracted squashfs-root exists" "[ -d '${EXTRACTED_DIR}' ]"
assert_true "AppRun entrypoint exists and is executable" "[ -x '${EXTRACTED_DIR}/AppRun' ]"
assert_true "Desktop file exists at root" "[ -f '${EXTRACTED_DIR}/org.egoboard.Egoboard.desktop' ]"
assert_true "Application icon exists at root" "[ -f '${EXTRACTED_DIR}/egoboard.svg' ]"
assert_true ".DirIcon link/file exists" "[ -e '${EXTRACTED_DIR}/.DirIcon' ]"
assert_true "Main binary exists and is executable" "[ -x '${EXTRACTED_DIR}/usr/bin/egoboard' ]"

# ------------------------------------------------------------------------------
# 3. Inspect Essential Qt 6 & KF6 Bundled Plugins
# ------------------------------------------------------------------------------
log_info "Inspecting essential Qt 6 plugins inside AppDir..."

assert_true "SQLite driver plugin present (libqsqlite.so)" \
    "[ -f '${EXTRACTED_DIR}/usr/plugins/sqldrivers/libqsqlite.so' ]"

assert_true "XCB platform plugin present (libqxcb.so)" \
    "[ -f '${EXTRACTED_DIR}/usr/plugins/platforms/libqxcb.so' ]"

assert_true "Wayland platform plugin present (libqwayland.so / libqwayland-generic.so / libqwayland-egl.so)" \
    "[ -f '${EXTRACTED_DIR}/usr/plugins/platforms/libqwayland.so' ] || [ -f '${EXTRACTED_DIR}/usr/plugins/platforms/libqwayland-generic.so' ] || [ -f '${EXTRACTED_DIR}/usr/plugins/platforms/libqwayland-egl.so' ]"

assert_true "SVG image format plugin present" \
    "[ -f '${EXTRACTED_DIR}/usr/plugins/imageformats/libqsvg.so' ] || [ -f '${EXTRACTED_DIR}/usr/plugins/iconengines/libqsvgicon.so' ]"

# ------------------------------------------------------------------------------
# 4. Smoke Test Execution
# ------------------------------------------------------------------------------
log_info "Executing headless smoke test from AppImage..."

SMOKE_OUTPUT=""
SMOKE_EXIT_CODE=0
export APPIMAGE_EXTRACT_AND_RUN=1

if SMOKE_OUTPUT="$(QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" "${APPIMAGE_PATH}" --smoke 2>&1)"; then
    SMOKE_EXIT_CODE=0
else
    SMOKE_EXIT_CODE=$?
fi

echo "${SMOKE_OUTPUT}"

assert_true "AppImage --smoke returns exit code 0" "[ ${SMOKE_EXIT_CODE} -eq 0 ]"
if [ ${SMOKE_EXIT_CODE} -eq 0 ]; then
    if echo "${SMOKE_OUTPUT}" | grep -q 'egoboard smoke test: OK'; then
        log_pass "Smoke test output confirms success"
    else
        log_warn "Smoke test exited successfully without emitting the optional success message"
    fi
else
    log_fail "Smoke test did not complete successfully"
    FAILURES=$((FAILURES + 1))
fi

# ------------------------------------------------------------------------------
# 5. Final Report
# ------------------------------------------------------------------------------
echo -e "\n${BOLD}======================================================${RESET}"
if [ ${FAILURES} -eq 0 ]; then
    log_pass "All AppImage validation tests passed successfully!"
    echo -e "${GREEN}${BOLD}Egoboard AppImage is verified and ready for distribution.${RESET}\n"
    exit 0
else
    log_fail "${FAILURES} validation test(s) failed."
    exit 1
fi
