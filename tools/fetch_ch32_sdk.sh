#!/usr/bin/env bash
set -euo pipefail

SDK_DIR="${1:?usage: fetch_ch32_sdk.sh <build-directory/ch32-sdk>}"
SDK_COMMIT=b375bbbc7dfe8642d6fb7c17e6d3207ee1c3c9c1
SDK_URL=https://github.com/openwch/ch32v20x.git

if [ -e "${SDK_DIR}" ] && [ ! -d "${SDK_DIR}/.git" ]; then
    echo "Refusing to use an existing non-Git SDK directory: ${SDK_DIR}" >&2
    exit 1
fi
if [ ! -d "${SDK_DIR}/.git" ]; then
    git init --quiet "${SDK_DIR}"
fi
if [ -n "$(git -C "${SDK_DIR}" status --porcelain)" ]; then
    echo "Refusing to change a modified SDK checkout: ${SDK_DIR}" >&2
    exit 1
fi
if [ "$(git -C "${SDK_DIR}" rev-parse --verify HEAD 2>/dev/null || true)" != "${SDK_COMMIT}" ]; then
    git -C "${SDK_DIR}" fetch --depth 1 "${SDK_URL}" "${SDK_COMMIT}"
    git -C "${SDK_DIR}" checkout --quiet --detach "${SDK_COMMIT}"
fi
test "$(git -C "${SDK_DIR}" rev-parse HEAD)" = "${SDK_COMMIT}"
echo "CH32 SDK: ${SDK_COMMIT} (${SDK_DIR})"
