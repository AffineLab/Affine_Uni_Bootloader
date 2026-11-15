#!/usr/bin/env bash
set -euo pipefail
TOOL_ROOT="${1:?usage: fetch_picotool.sh <build-directory/picotool> <pico-sdk>}"
SDK_DIR="$(cd "${2:?pico-sdk directory required}" && pwd)"
TOOL_COMMIT=2041936441b48a3cc53ae3da9e805229fe8f4e18
TOOL_SOURCE="${TOOL_ROOT}/src"
if [ -e "${TOOL_SOURCE}" ] && [ ! -d "${TOOL_SOURCE}/.git" ]; then
    echo "Refusing existing non-Git picotool source: ${TOOL_SOURCE}" >&2; exit 1
fi
if [ ! -d "${TOOL_SOURCE}/.git" ]; then git init --quiet "${TOOL_SOURCE}"; fi
if [ -n "$(git -C "${TOOL_SOURCE}" status --porcelain)" ]; then
    echo "Refusing modified picotool checkout: ${TOOL_SOURCE}" >&2; exit 1
fi
if [ "$(git -C "${TOOL_SOURCE}" rev-parse --verify HEAD 2>/dev/null || true)" != "${TOOL_COMMIT}" ]; then
    git -C "${TOOL_SOURCE}" fetch --depth 1 https://github.com/raspberrypi/picotool.git "${TOOL_COMMIT}"
    git -C "${TOOL_SOURCE}" checkout --quiet --detach "${TOOL_COMMIT}"
fi
test "$(git -C "${TOOL_SOURCE}" rev-parse HEAD)" = "${TOOL_COMMIT}"
cmake -S "${TOOL_SOURCE}" -B "${TOOL_ROOT}/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DPICOTOOL_NO_LIBUSB=1 -DPICOTOOL_FLAT_INSTALL=1 \
    -DGIT_SUBMODULE=OFF -DUSE_PRECOMPILED=ON \
    "-DPICO_SDK_PATH=${SDK_DIR}" "-DCMAKE_INSTALL_PREFIX=$(cd "${TOOL_ROOT}" && pwd)/install"
cmake --build "${TOOL_ROOT}/build" --target install --parallel 2
"${TOOL_ROOT}/install/picotool/picotool" version 2.3.1
