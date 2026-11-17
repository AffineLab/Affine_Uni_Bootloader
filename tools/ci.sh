#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

BUILD_ROOT="${BUILD_ROOT:-build-ci}"
BOOTLOADER_SIZE_LIMIT="${BOOTLOADER_SIZE_LIMIT:-24576}"
BOOTLOADER_SIZE_WARN="${BOOTLOADER_SIZE_WARN:-22528}"

cmake_configure_build() {
    local target_name="$1"
    local build_dir="$2"
    local cmake_flag="$3"
    local ninja_target="$4"

    cmake -S . -B "${build_dir}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-gcc.cmake \
        "${cmake_flag}=ON"
    ninja -C "${build_dir}" "${ninja_target}"
}

check_size() {
    local target_name="$1"
    local path="$2"
    local limit="${3:-${BOOTLOADER_SIZE_LIMIT}}"
    local warn="${4:-${BOOTLOADER_SIZE_WARN}}"
    local size

    size="$(stat -c '%s' "${path}")"
    echo "${target_name} size: ${size} bytes"

    if [ "${size}" -gt "${limit}" ]; then
        echo "${target_name} exceeds ${limit} byte bootloader limit" >&2
        return 1
    fi

    if [ "${size}" -gt "${warn}" ]; then
        echo "${target_name} is above warning threshold ${warn} bytes" >&2
    fi
}

run_manifest_smoke() {
    local fixture_dir="${BUILD_ROOT}/fixtures"
    local smoke_dir="${BUILD_ROOT}/manifest-smoke"
    local key_path="${smoke_dir}/manifest-test-rsa.pem"
    local image_path="${fixture_dir}/sample-app.bin"

    mkdir -p "${fixture_dir}" "${smoke_dir}"

    python3 - "${image_path}" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
image = bytearray([0xFF] * 256)
image[0:8] = bytes.fromhex("0080002009600008")
path.write_bytes(image)
PY

    openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 \
        -out "${key_path}" >/dev/null 2>&1

    python3 tools/boot_test.py COM0 flash "${image_path}" \
        --dry-run \
        --target-id 0x47343331 \
        --manifest-key "${key_path}" \
        --manifest-out "${smoke_dir}/signed.manifest"

    python3 tools/boot_test.py COM0 flash "${image_path}" \
        --dry-run \
        --target-id 0x47343331 \
        --manifest-key "${key_path}" \
        --anti-rollback \
        --manifest-out "${smoke_dir}/signed-anti-rollback.manifest"

    python3 tools/boot_test.py COM0 flash "${image_path}" \
        --dry-run \
        --target-id 0x47343331 \
        --manifest-key "${key_path}" \
        --encrypted \
        --anti-rollback \
        --manifest-nonce 000102030405060708090A0B0C0D0E0F \
        --manifest-out "${smoke_dir}/signed-encrypted-anti-rollback.manifest"
}

# Start from an empty BUILD_ROOT, except for the SDK the workflow fetched there.
mkdir -p "${BUILD_ROOT}"
find "${BUILD_ROOT}" -mindepth 1 -maxdepth 1 ! -name at32-sdk ! -name ch32-sdk ! -name pico-sdk ! -name picotool -exec rm -rf {} +
AT32_SDK_COMMIT=5dd9d55a2ce9ffa8fe0cb2652ac142920f2094a3
if [ -z "${AFFINE_AT32_SDK_DIR:-}" ]; then
    AFFINE_AT32_SDK_DIR="${BUILD_ROOT}/at32-sdk"
    bash tools/fetch_at32_sdk.sh "${AFFINE_AT32_SDK_DIR}"
fi
# A supplied SDK is read, never fetched into or modified. CMake checks its
# version macros; a Git checkout must also be at the pinned commit.
if [ -e "${AFFINE_AT32_SDK_DIR}/.git" ]; then
    test "$(git -C "${AFFINE_AT32_SDK_DIR}" rev-parse HEAD)" = "${AT32_SDK_COMMIT}"
fi
AFFINE_AT32_SDK_DIR="$(cd "${AFFINE_AT32_SDK_DIR}" && pwd)"
if [ -z "${PICO_SDK_PATH:-}" ]; then
    PICO_SDK_PATH="${BUILD_ROOT}/pico-sdk"
    bash tools/fetch_pico_sdk.sh "${PICO_SDK_PATH}"
fi
PICO_SDK_PATH="$(cd "${PICO_SDK_PATH}" && pwd)"
if [ -z "${picotool_DIR:-}" ]; then
    bash tools/fetch_picotool.sh "${BUILD_ROOT}/picotool" "${PICO_SDK_PATH}"
    picotool_DIR="$(cd "${BUILD_ROOT}/picotool/install/picotool" && pwd)"
fi

if [ -z "${AFFINE_CH32_SDK_DIR:-}" ]; then
    AFFINE_CH32_SDK_DIR="${BUILD_ROOT}/ch32-sdk"
    bash tools/fetch_ch32_sdk.sh "${AFFINE_CH32_SDK_DIR}"
fi
AFFINE_CH32_SDK_DIR="$(cd "${AFFINE_CH32_SDK_DIR}" && pwd)"

python3 -m py_compile tools/boot_test.py
# The UART options parse (a dry run opens no port).
python3 tools/boot_test.py COM0 probe --dry-run --transport uart --baud 115200
git diff --check -- . ':!third_party'

run_manifest_smoke

cmake_configure_build \
    STM32F072 \
    "${BUILD_ROOT}/stm32f072" \
    -DAFFINE_BUILD_STM32F072 \
    bootloader_stm32f072

# The same target over a USART instead of USB (AFFINE_BOOT_TRANSPORT=uart).
cmake -S . -B "${BUILD_ROOT}/stm32f072-uart" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-gcc.cmake \
    -DAFFINE_BUILD_STM32F072=ON -DAFFINE_BOOT_TRANSPORT=uart
ninja -C "${BUILD_ROOT}/stm32f072-uart" bootloader_stm32f072

# USB CDC and a USART at once (AFFINE_BOOT_TRANSPORT=dual).
cmake -S . -B "${BUILD_ROOT}/stm32f072-dual" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-gcc.cmake \
    -DAFFINE_BUILD_STM32F072=ON -DAFFINE_BOOT_TRANSPORT=dual
ninja -C "${BUILD_ROOT}/stm32f072-dual" bootloader_stm32f072

cmake_configure_build \
    STM32G431 \
    "${BUILD_ROOT}/stm32g431" \
    -DAFFINE_BUILD_STM32G431 \
    bootloader_stm32g431
# The updater embeds the stamped, guarded bootloader built above and refuses
# an unstamped one, so building it also checks the stamping.
ninja -C "${BUILD_ROOT}/stm32g431" g431_bl_updater

cmake_configure_build \
    STM32H503 \
    "${BUILD_ROOT}/stm32h503" \
    -DAFFINE_BUILD_STM32H503 \
    bootloader_stm32h503

check_size STM32F072 "${BUILD_ROOT}/stm32f072/bootloader_stm32f072.bin"
check_size STM32F072-UART "${BUILD_ROOT}/stm32f072-uart/bootloader_stm32f072.bin"
check_size STM32F072-DUAL "${BUILD_ROOT}/stm32f072-dual/bootloader_stm32f072.bin"
check_size STM32G431 "${BUILD_ROOT}/stm32g431/bootloader_stm32g431.bin"
check_size STM32H503 "${BUILD_ROOT}/stm32h503/bootloader_stm32h503.bin"

for usb in otgfs1 otghs; do
    cmake -S . -B "${BUILD_ROOT}/at32f405-${usb}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-gcc.cmake \
        -DAFFINE_BUILD_AT32F405=ON "-DAFFINE_BOOT_AT32_USB=${usb}" \
        "-DAFFINE_AT32_SDK_DIR=${AFFINE_AT32_SDK_DIR}"
    ninja -C "${BUILD_ROOT}/at32f405-${usb}" bootloader_at32f405
    check_size "AT32F405-${usb}" "${BUILD_ROOT}/at32f405-${usb}/bootloader_at32f405.bin"
done

for usb in usbd usbfs; do
    cmake -S . -B "${BUILD_ROOT}/ch32v203-${usb}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-none-elf-gcc.cmake \
        -DAFFINE_BUILD_CH32V203=ON "-DAFFINE_BOOT_CH32_USB=${usb}" \
        "-DAFFINE_CH32_SDK_DIR=${AFFINE_CH32_SDK_DIR}"
    ninja -C "${BUILD_ROOT}/ch32v203-${usb}" bootloader_ch32v203
    check_size "CH32V203-${usb}" "${BUILD_ROOT}/ch32v203-${usb}/bootloader_ch32v203.bin" 24576 22528
    python3 tools/check_ch32.py "${BUILD_ROOT}/ch32v203-${usb}"
done

# 28 KiB: the last sector of the 32 KiB boot region holds the E10 UF2 block.
for board in pico2 rp2354; do
    cmake -S targets/rp2350 -B "${BUILD_ROOT}/rp2350-${board}" -G Ninja \
        -DCMAKE_BUILD_TYPE=MinSizeRel "-DAFFINE_RP2350_BOARD=${board}" \
        "-DPICO_SDK_PATH=${PICO_SDK_PATH}" "-Dpicotool_DIR=${picotool_DIR}"
    cmake --build "${BUILD_ROOT}/rp2350-${board}"
    check_size "RP2350-${board}" "${BUILD_ROOT}/rp2350-${board}/bootloader_rp2350.bin" 28672 27648
    python3 tools/check_rp2350.py "${BUILD_ROOT}/rp2350-${board}"
done

# Core/dual transport plus AT32/RP2350/CH32 adapters; all hardware interfaces are faked.
python3 tests/host/run.py --build-dir "${BUILD_ROOT}/host" \
    --at32-sdk-dir "${AFFINE_AT32_SDK_DIR}" --pico-sdk-dir "${PICO_SDK_PATH}"
