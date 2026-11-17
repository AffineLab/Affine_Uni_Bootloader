#!/usr/bin/env bash
set -euo pipefail
SDK_DIR="${1:?usage: fetch_pico_sdk.sh <build-directory/pico-sdk>}"
SDK_COMMIT=079c6f39023649b154152db30f1d781e884879bc
TINYUSB_COMMIT=86ad6e56c1700e85f1c5678607a762cfe3aa2f47
if [ -e "${SDK_DIR}" ] && [ ! -d "${SDK_DIR}/.git" ]; then
    echo "Refusing existing non-Git SDK directory: ${SDK_DIR}" >&2; exit 1
fi
if [ ! -d "${SDK_DIR}/.git" ]; then git init --quiet "${SDK_DIR}"; fi
if [ -n "$(git -C "${SDK_DIR}" status --porcelain)" ]; then
    echo "Refusing modified SDK checkout: ${SDK_DIR}" >&2; exit 1
fi
if [ "$(git -C "${SDK_DIR}" rev-parse --verify HEAD 2>/dev/null || true)" != "${SDK_COMMIT}" ]; then
    git -C "${SDK_DIR}" fetch --depth 1 https://github.com/raspberrypi/pico-sdk.git "${SDK_COMMIT}"
    git -C "${SDK_DIR}" checkout --quiet --detach "${SDK_COMMIT}"
fi
# Only TinyUSB, without its optional nested submodules or any other SDK library.
git -C "${SDK_DIR}" submodule update --init --depth 1 -- lib/tinyusb
test "$(git -C "${SDK_DIR}" rev-parse HEAD)" = "${SDK_COMMIT}"
test "$(git -C "${SDK_DIR}/lib/tinyusb" rev-parse HEAD)" = "${TINYUSB_COMMIT}"
echo "pico-sdk ${SDK_COMMIT}; TinyUSB ${TINYUSB_COMMIT}"
