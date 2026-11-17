#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"
: "${IDF_PATH:?Set IDF_PATH to ESP-IDF v6.1.1}"
# This script runs in the pinned Linux IDF container or an IDF Linux shell.
# shellcheck disable=SC1091
. "${IDF_PATH}/export.sh"

idf.py -C targets/esp32s3/agent build size
idf.py -C targets/esp32s3/test_app build size

# Test-only variants, kept compiling: the agent with its log CDC and
# 1200-baud download hook, and the test app that resets before confirming.
T="$(pwd)/targets/esp32s3"
idf.py -C targets/esp32s3/agent -B "$T/agent/build-logcdc"     -D "SDKCONFIG=$T/agent/build-logcdc/sdkconfig"     -D "SDKCONFIG_DEFAULTS=$T/sdkconfig.defaults;$T/agent/sdkconfig.defaults;$T/agent/sdkconfig.logcdc" build
grep -q '^CONFIG_AFFINE_AGENT_LOG_CDC=y' "$T/agent/build-logcdc/sdkconfig"
idf.py -C targets/esp32s3/test_app -B "$T/test_app/build-crash"     -D "SDKCONFIG=$T/test_app/build-crash/sdkconfig"     -D "SDKCONFIG_DEFAULTS=$T/sdkconfig.defaults;$T/test_app/sdkconfig.crash" build
grep -q '^CONFIG_AFFINE_TEST_CRASH=y' "$T/test_app/build-crash/sdkconfig"
# The release agent must not carry the test hooks (set -e ignores "! cmd").
if grep -q 'CONFIG_AFFINE_AGENT_LOG_CDC 1' "$T/agent/build/config/sdkconfig.h"; then
    echo "release agent has the test-only log CDC enabled" >&2
    exit 1
fi

python - <<'PY'
import pathlib

for project, limit in (("agent", 0x100000), ("test_app", 0x400000)):
    path = pathlib.Path("targets/esp32s3") / project / "build" / f"affine_esp32s3_{project}.bin"
    size = path.stat().st_size
    print(f"ESP32-S3 {project}: {size} bytes / {limit} bytes; {limit - size} bytes free")
    if size > limit:
        raise SystemExit(f"{project} exceeds its partition")
PY

SMOKE_DIR="${BUILD_ROOT:-build-ci}/esp32s3-manifest-smoke"
mkdir -p "${SMOKE_DIR}"
KEY_PATH="${SMOKE_DIR}/manifest-test-rsa.pem"
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "${KEY_PATH}" >/dev/null 2>&1
IMAGE_PATH="targets/esp32s3/test_app/build/affine_esp32s3_test_app.bin"

# Offline packaging checks only: COM0 is a placeholder; --dry-run opens no port.
python tools/boot_test.py COM0 flash "${IMAGE_PATH}" --dry-run \
    --target-id 0x45533353 --manifest-key "${KEY_PATH}" \
    --manifest-out "${SMOKE_DIR}/signed.manifest"
python tools/boot_test.py COM0 flash "${IMAGE_PATH}" --dry-run \
    --target-id 0x45533353 --manifest-key "${KEY_PATH}" --anti-rollback \
    --manifest-out "${SMOKE_DIR}/signed-anti-rollback.manifest"
python tools/boot_test.py COM0 flash "${IMAGE_PATH}" --dry-run \
    --target-id 0x45533353 --manifest-key "${KEY_PATH}" --encrypted --anti-rollback \
    --manifest-nonce 000102030405060708090A0B0C0D0E0F \
    --manifest-out "${SMOKE_DIR}/signed-encrypted-anti-rollback.manifest"
