#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${EGOBOARD_BUILD_DIR:-${ROOT_DIR}/build}"

"${SCRIPT_DIR}/build.sh"
QT_QPA_PLATFORM=offscreen ctest --test-dir "${BUILD_DIR}" --output-on-failure
QT_QPA_PLATFORM=offscreen "${BUILD_DIR}/egoboard" --smoke
