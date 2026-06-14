#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

BUILD_ROOT="${BUILD_ROOT:-build-release}"
DIST_DIR="${DIST_DIR:-dist/release}"
RELEASE_VERSION="${AFFINE_RELEASE_VERSION:-${GITHUB_REF_NAME:-local}}"
BOOTLOADER_SIZE_LIMIT="${BOOTLOADER_SIZE_LIMIT:-24576}"
BOOTLOADER_SIZE_WARN="${BOOTLOADER_SIZE_WARN:-22528}"
PRODUCTION_REQUIRE_ANTI_ROLLBACK="${AFFINE_RELEASE_PRODUCTION_REQUIRE_ANTI_ROLLBACK:-0}"

cmake_common=(
    -G Ninja
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-gcc.cmake
)

bool_value() {
    case "${1:-}" in
        1|ON|on|TRUE|true|YES|yes) echo 1 ;;
        *) echo 0 ;;
    esac
}

check_size() {
    local label="$1"
    local path="$2"
    local size

    size="$(stat -c '%s' "${path}")"
    echo "${label} size: ${size} bytes"

    if [ "${size}" -gt "${BOOTLOADER_SIZE_LIMIT}" ]; then
        echo "${label} exceeds ${BOOTLOADER_SIZE_LIMIT} byte bootloader limit" >&2
        return 1
    fi

    if [ "${size}" -gt "${BOOTLOADER_SIZE_WARN}" ]; then
        echo "${label} is above warning threshold ${BOOTLOADER_SIZE_WARN} bytes" >&2
    fi
}

write_secret_file() {
    local value="$1"
    local path="$2"

    umask 077
    printf '%s\n' "${value}" > "${path}"
}

extract_public_key() {
    local input_path="$1"
    local output_path="$2"

    if openssl rsa -pubin -in "${input_path}" -pubout -out "${output_path}" >/dev/null 2>&1; then
        return 0
    fi

    openssl rsa -in "${input_path}" -pubout -out "${output_path}" >/dev/null 2>&1
}

prepare_production_keys() {
    local key_dir="${BUILD_ROOT}/keys"
    local input_key_path="${AFFINE_BOOT_RELEASE_PUBLIC_KEY_FILE:-}"
    local public_key_path="${key_dir}/production_public_key.pem"
    local generated_header="${key_dir}/boot_keys_release.h"
    local aes_key="${AFFINE_BOOT_RELEASE_AES128_KEY_HEX:-}"

    mkdir -p "${key_dir}"

    if [ -z "${input_key_path}" ]; then
        if [ -z "${AFFINE_BOOT_RELEASE_PUBLIC_KEY_PEM:-}" ]; then
            echo "AFFINE_BOOT_RELEASE_PUBLIC_KEY_FILE or AFFINE_BOOT_RELEASE_PUBLIC_KEY_PEM is required for production builds" >&2
            return 1
        fi
        input_key_path="${key_dir}/release_public_key_input.pem"
        write_secret_file "${AFFINE_BOOT_RELEASE_PUBLIC_KEY_PEM}" "${input_key_path}"
    fi

    if [ -z "${aes_key}" ]; then
        echo "AFFINE_BOOT_RELEASE_AES128_KEY_HEX is required for production builds" >&2
        return 1
    fi

    extract_public_key "${input_key_path}" "${public_key_path}"
    python3 tools/generate_boot_keys_header.py \
        --public-key "${public_key_path}" \
        --aes-key-hex "${aes_key}" \
        --out "${generated_header}"

    cp "${public_key_path}" "${DIST_DIR}/production_public_key.pem"
    printf '%s\n' "${generated_header}"
}

build_target_variant() {
    local target="$1"
    local cmake_flag="$2"
    local ninja_target="$3"
    local variant="$4"
    local allow_unsigned="$5"
    local require_signed="$6"
    local allow_optional_anti="$7"
    local require_anti="$8"
    local key_header="${9:-}"
    local build_dir="${BUILD_ROOT}/${target}-${variant}"
    local output_base="bootloader_${target}_${variant}"

    local cmake_args=(
        "${cmake_common[@]}"
        "${cmake_flag}=ON"
        -DAFFINE_BOOT_ALLOW_UNSIGNED_UPDATES="${allow_unsigned}"
        -DAFFINE_BOOT_REQUIRE_SIGNED_UPDATES="${require_signed}"
        -DAFFINE_BOOT_ALLOW_OPTIONAL_ANTI_ROLLBACK="${allow_optional_anti}"
        -DAFFINE_BOOT_REQUIRE_ANTI_ROLLBACK_FOR_SIGNED="${require_anti}"
        -DAFFINE_BOOT_PRESERVE_SECURITY_STATE_ON_CLEAR_METADATA=1
    )

    if [ -n "${key_header}" ]; then
        cmake_args+=(-DAFFINE_BOOT_KEYS_HEADER="${key_header}")
    fi

    cmake -S . -B "${build_dir}" "${cmake_args[@]}"
    ninja -C "${build_dir}" "${ninja_target}"

    cp "${build_dir}/${ninja_target}.bin" "${DIST_DIR}/${output_base}.bin"
    cp "${build_dir}/${ninja_target}.elf" "${DIST_DIR}/${output_base}.elf"
    cp "${build_dir}/${ninja_target}.map" "${DIST_DIR}/${output_base}.map"
    check_size "${target}-${variant}" "${DIST_DIR}/${output_base}.bin"
}

write_release_metadata() {
    local commit
    commit="$(git rev-parse --short=12 HEAD 2>/dev/null || printf unknown)"

    (
        cd "${DIST_DIR}"
        sha256sum bootloader_* production_public_key.pem > SHA256SUMS.txt
    )

    python3 - "${DIST_DIR}" "${RELEASE_VERSION}" "${commit}" "${PRODUCTION_REQUIRE_ANTI_ROLLBACK}" <<'PY'
import hashlib
import json
import pathlib
import re
import sys

dist = pathlib.Path(sys.argv[1])
version = sys.argv[2]
commit = sys.argv[3]
production_require_anti = sys.argv[4] not in ("0", "OFF", "off", "false", "FALSE", "")
protocol_text = pathlib.Path("include/boot_protocol.h").read_text(encoding="utf-8")
match = re.search(r"#define\s+BOOT_PROTOCOL_VERSION\s+(0x[0-9A-Fa-f]+|\d+)", protocol_text)
protocol_version = match.group(1) if match else "unknown"

artifacts = []
for path in sorted(dist.glob("bootloader_*.*")):
    match = re.match(r"bootloader_(stm32g431|stm32h503)_(unsigned|production)\.(bin|elf|map)$", path.name)
    if not match:
        continue
    target, variant, kind = match.groups()
    data = path.read_bytes()
    policy = {
        "allow_unsigned_updates": variant == "unsigned",
        "require_signed_updates": variant == "production",
        "allow_optional_anti_rollback": True,
        "require_anti_rollback_for_signed": production_require_anti if variant == "production" else False,
    }
    artifacts.append({
        "file": path.name,
        "target": target,
        "variant": variant,
        "kind": kind,
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "policy": policy,
    })

public_key = dist / "production_public_key.pem"
manifest = {
    "release": version,
    "source_commit": commit,
    "protocol_version": protocol_version,
    "bootloader_size_limit": 24576,
    "production_public_key": {
        "file": public_key.name,
        "bytes": public_key.stat().st_size,
        "sha256": hashlib.sha256(public_key.read_bytes()).hexdigest(),
    },
    "artifacts": artifacts,
}
(dist / "RELEASE_MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

rows = [
    "| Target | Variant | BIN | Size | SHA-256 |",
    "| --- | --- | --- | ---: | --- |",
]
for item in artifacts:
    if item["kind"] != "bin":
        continue
    rows.append(
        f"| `{item['target']}` | `{item['variant']}` | `{item['file']}` | "
        f"{item['bytes']} | `{item['sha256']}` |"
    )

notes = f"""# Affine Uni Bootloader {version}

Source commit: `{commit}`

Protocol version: `{protocol_version}`

Production public key: `{public_key.name}`

## Bootloader Binaries

{chr(10).join(rows)}

## Variants

- `unsigned`: allows unsigned updates and signed manifest updates. Use for development, bring-up, and recovery-friendly deployments.
- `production`: requires signed manifest updates and rejects unsigned committed applications. Anti-rollback is {"required for every signed update" if production_require_anti else "available when requested by the signed manifest"}.

## Flash Layout

- STM32G431 bootloader: `0x08000000`-`0x08005FFF`; app slot starts at `0x08006000`.
- STM32H503 bootloader: `0x08000000`-`0x08005FFF`; app slot starts at `0x08006000`.

See `RELEASE_MANIFEST.json` and `SHA256SUMS.txt` for machine-readable metadata.
"""
(dist / "RELEASE_NOTES.md").write_text(notes, encoding="utf-8")
PY
}

rm -rf "${BUILD_ROOT}" "${DIST_DIR}"
mkdir -p "${BUILD_ROOT}" "${DIST_DIR}"

production_key_header="$(prepare_production_keys)"

build_target_variant stm32g431 -DAFFINE_BUILD_STM32G431 bootloader_stm32g431 unsigned 1 0 1 0
build_target_variant stm32h503 -DAFFINE_BUILD_STM32H503 bootloader_stm32h503 unsigned 1 0 1 0

build_target_variant stm32g431 -DAFFINE_BUILD_STM32G431 bootloader_stm32g431 production 0 1 1 "$(bool_value "${PRODUCTION_REQUIRE_ANTI_ROLLBACK}")" "${production_key_header}"
build_target_variant stm32h503 -DAFFINE_BUILD_STM32H503 bootloader_stm32h503 production 0 1 1 "$(bool_value "${PRODUCTION_REQUIRE_ANTI_ROLLBACK}")" "${production_key_header}"

write_release_metadata

echo "Release artifacts written to ${DIST_DIR}"
