# Affine Unified USB-CDC Bootloader

A target-portable STM32 USB-CDC bootloader with a framed firmware update protocol, metadata-based image validation, and host-side flashing tools.

## Overview

Affine Unified USB-CDC Bootloader separates the reusable bootloader core from MCU-specific platform code. The shared core owns protocol framing, CRC validation, download session state, image metadata, and application handoff. Each target supplies flash operations, USB CDC transport glue, memory layout, and reset/jump behavior.

The project is intended for STM32-based devices that need a compact firmware update path over USB CDC without forcing a private header into the application image.

## Current Targets

| Target | Status |
| --- | --- |
| `STM32F072` | Platform layer, USB CDC transport, linker scripts, and CMake target are present. |
| `STM32G431` | Platform layer, USB CDC transport, linker scripts, and CMake target are present. |
| `STM32H503` | Platform layer, USB CDC transport, linker scripts, and CMake target are present. |
| `STM32F401` | Memory layout template only. |
| `stub` | Minimal host/stub platform for core bring-up. |

## Device Identity

USB product strings are intentionally shared as `Affine Uni Bootloader`. Board and layout compatibility is reported by protocol instead of product text. `GET_DEVICE_INFO` returns the MCU target ID, board ID, board revision, flash layout ID, memory layout, write alignment, and security capabilities.
Firmware-side IDs are defined in [include/boot_target_ids.h](include/boot_target_ids.h).

| Target | Board | USB PID | `target_id` | `board_id` | `flash_layout_id` |
| --- | --- | --- | --- | --- | --- |
| `STM32F072` | `Monica_NFC` | `0x52B2` | `0x46303732` | `0x4D4F4E43` | `0x46303741` |
| `STM32G431` | `Mai` | `0x52B0` | `0x47343331` | `0x4D414931` | `0x47343341` |
| `STM32H503` | `LinneaPro` | `0x52B1` | `0x48353033` | `0x4C504835` | `0x48353041` |

## Repository Layout

```text
include/                  Public bootloader interfaces
src/                      Shared protocol, image, security, and state-machine code
targets/                  MCU-specific platform implementations and linker scripts
targets/common/           Shared target-side helpers for flash range checks and USB transport
third_party/              Vendored STM32Cube HAL/CMSIS/startup and ST USB middleware subsets
cmake/toolchains/         Cross-compilation toolchain files
tools/boot_test.py        Host-side USB CDC smoke test and flashing utility
```

## Protocol Summary

The transport is a USB CDC byte stream. The bootloader protocol adds its own fixed header, payload length, sequence number, payload CRC32, and header CRC32.

Main commands:

- `HELLO`: query protocol version, target ID, application base address, slot size, and maximum chunk size.
- `GET_DEVICE_INFO`: query formal target, board, flash layout, memory geometry, write alignment, and capability fields.
- `BEGIN`: start a firmware download session for a specific target, board, and flash layout, then erase the application pages covered by the image.
- `DATA`: write one aligned firmware chunk at the next expected offset.
- `COMMIT`: verify the received image, read back flash CRC, and write committed metadata.
- `ABORT`: cancel the active session.
- `GET_STATUS`: query current state, error code, written size, rolling CRC, current operation, and operation progress.
- `GET_DIAG`: query transport and frame decoder diagnostics.
- `GET_METADATA`: query the selected valid metadata copy without running a full image CRC.
- `CLEAR_METADATA`: erase application metadata copies and leave the device in bootloader mode. Persistent security state is preserved by default.
- `SET_MANIFEST`: provide a signed firmware manifest after `BEGIN` and before `DATA`.
- `VERIFY_IMAGE`: validate selected metadata, vector table, full image CRC, and security policy.
- `REBOOT`: acknowledge the command and reset the MCU.
- `BOOT_APP`: acknowledge the command, then jump to the committed application if it validates.

## Image Layout

Applications remain standard bare-metal images:

- `APP_BASE + 0x00` is the initial stack pointer.
- `APP_BASE + 0x04` is the reset handler.
- No private bootloader header is inserted before the vector table.

The bootloader stores CRC-protected image metadata in a separate flash region near the end of the application slot. Targets with enough reserved metadata space keep two erase-unit metadata copies and use a valid copy on boot. On boot, the bootloader validates metadata, vector table addresses, and image CRC before jumping to the application.

STM32F072 is a Cortex-M0 target and has no VTOR register. Its bootloader copies the application vector table to the first `0xC0` bytes of SRAM and remaps SRAM at address zero before jumping, so STM32F072 applications must reserve `0x20000000`-`0x200000BF`. The provided `targets/stm32f072/STM32F072CBTX_APP.ld` starts application RAM at `0x200000C0`.

Supported targets reserve 24 KiB for the bootloader:

| Target | Bootloader | Application slot | Metadata |
| --- | --- | --- | --- |
| `STM32F072` | `0x08000000`-`0x08005FFF` | `0x08006000`-`0x0801F7FF` (102 KiB) | `0x0801F800`-`0x0801FFFF` |
| `STM32G431` | `0x08000000`-`0x08005FFF` | `0x08006000`-`0x0801DFFF` (96 KiB) | `0x0801E000`-`0x0801FFFF` |
| `STM32H503` | `0x08000000`-`0x08005FFF` | `0x08006000`-`0x0801BFFF` (88 KiB) | `0x0801C000`-`0x0801FFFF` |

## Security Model

The bootloader supports both unsigned development updates and signed secure updates.

Security flags:

| Flag | Meaning |
| --- | --- |
| `BOOT_FLAG_VERIFY_SIGNATURE` | Require a signed manifest before accepting image data. |
| `BOOT_FLAG_ENCRYPTED_IMAGE` | Treat incoming `DATA` chunks as AES-128 CTR ciphertext. This flag also requires a signed manifest. |
| `BOOT_FLAG_ANTI_ROLLBACK` | Request rollback-floor enforcement for this signed update. |

Signed manifest updates use `SET_MANIFEST` after `BEGIN` and before the first `DATA` frame. The manifest includes target ID, board ID, flash layout ID, image size, CRC32, firmware version, flags, key ID, SHA-256 of the plaintext image, AES-CTR nonce, and an RSA-2048 PKCS#1 v1.5/SHA-256 signature. The bootloader stores the signed manifest fields in metadata and verifies the signature, board/layout compatibility, and SHA-256 again before booting an application.

Unsigned development updates and signed secure updates can coexist. The default build allows unsigned updates so development and factory recovery remain simple. Production builds can change this policy with compile-time macros in [include/boot_policy.h](include/boot_policy.h):

| Macro | Default | Meaning |
| --- | --- | --- |
| `AFFINE_BOOT_ALLOW_UNSIGNED_UPDATES` | `1` | Allow `BEGIN` with no signature flag. |
| `AFFINE_BOOT_REQUIRE_SIGNED_UPDATES` | `0` | Reject unsigned updates and unsigned committed apps. |
| `AFFINE_BOOT_ALLOW_OPTIONAL_ANTI_ROLLBACK` | `1` | Allow a signed manifest to opt into rollback-floor updates with `BOOT_FLAG_ANTI_ROLLBACK`. |
| `AFFINE_BOOT_REQUIRE_ANTI_ROLLBACK_FOR_SIGNED` | `0` | Treat every signed update as anti-rollback protected, even if the flag is omitted. |
| `AFFINE_BOOT_PRESERVE_SECURITY_STATE_ON_CLEAR_METADATA` | `1` | Keep rollback-floor state when application metadata is cleared. |

Anti-rollback uses a small CRC-protected persistent security state stored beside each metadata copy. When an anti-rollback-protected signed update commits, the bootloader stores `max(previous_floor, firmware_version)` as the rollback floor. Later protected updates and boot validation reject firmware versions below that floor. `CLEAR_METADATA` does not clear this state by default, so clearing app metadata cannot reset the rollback floor.

This is still software-level rollback resistance. Production hardware should also use STM32 readout protection and, where available, option-byte or secure-storage policy for stronger rollback resistance.

Encrypted stream updates decrypt each incoming chunk with AES-128 CTR before writing flash. The default AES key and RSA public key in [include/boot_keys.h](include/boot_keys.h) are development keys and must be replaced for production. Firmware confidentiality also requires MCU readout protection; otherwise an attacker can read the symmetric key from flash.

ST provides full security reference packages such as X-CUBE-SBSFU and cryptographic libraries such as X-CUBE-CRYPTOLIB. This project keeps a compact self-contained implementation for the current 24 KiB bootloader budget, but production products should evaluate replacing the local crypto primitives with ST's maintained cryptographic library or a similarly reviewed backend.

## Building

This project uses CMake. The bootloader core and target glue live in this repository. Minimal STM32Cube HAL/CMSIS/startup and ST USB middleware subsets needed for the supported targets are vendored under `third_party/`.

Vendored third-party source layout:

| Path | Purpose |
| --- | --- |
| `third_party/stm32cube_f0` | STM32F072 HAL/CMSIS/startup plus USB Device middleware files used by the F072 build. |
| `third_party/stm32cube_g4` | STM32G431 HAL/CMSIS/startup plus USB Device middleware files used by the G431 build. |
| `third_party/stm32cube_h5` | STM32H503 HAL/CMSIS/startup plus USB Device middleware files used by the H503 build. |

The vendored files keep their original third-party copyright and license headers. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Toolchain discovery:

The Arm GNU toolchain is resolved from `PATH` by default. If it is not on `PATH`, point CMake at either the toolchain root or its `bin` directory.

| Variable | Purpose |
| --- | --- |
| `AFFINE_ARM_NONE_EABI_TOOLCHAIN_ROOT` | Optional `arm-none-eabi` toolchain root or `bin` directory. `ARM_NONE_EABI_TOOLCHAIN_ROOT` can also be set in the environment. |

Example STM32H503 build:

```powershell
cmake -S . -B build-stm32h503 `
  -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-gcc.cmake `
  -DAFFINE_BUILD_STM32H503=ON

ninja -C build-stm32h503 bootloader_stm32h503
```

Example STM32F072 build:

```powershell
cmake -S . -B build-stm32f072 `
  -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-gcc.cmake `
  -DAFFINE_BUILD_STM32F072=ON

ninja -C build-stm32f072 bootloader_stm32f072
```

Example STM32G431 build:

```powershell
cmake -S . -B build-stm32g431 `
  -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-gcc.cmake `
  -DAFFINE_BUILD_STM32G431=ON

ninja -C build-stm32g431 bootloader_stm32g431
```

## Host Tool

`tools/boot_test.py` provides a small serial utility for smoke testing and firmware flashing.

```powershell
py tools\boot_test.py COM5 hello
py tools\boot_test.py COM5 device-info
py tools\boot_test.py COM5 status
py tools\boot_test.py COM5 metadata
py tools\boot_test.py COM5 verify-image
py tools\boot_test.py COM5 probe
py tools\boot_test.py COM5 flash firmware.bin --run
py tools\boot_test.py COM5 flash firmware.bin --manifest-key release_rsa.pem
py tools\boot_test.py COM5 flash firmware.bin --manifest-key release_rsa.pem --encrypted
py tools\boot_test.py COM3 flash firmware.bin --app-jump --run --wait-app
py tools\boot_test.py COM5 clear-metadata
py tools\boot_test.py COM5 reboot
```

Offline manifest generation for the G431 target:

```powershell
py tools\boot_test.py COM0 flash firmware.bin `
  --dry-run `
  --target-id 0x47343331 `
  --board-id 0x4D414931 `
  --flash-layout-id 0x47343341 `
  --manifest-key release_rsa.pem `
  --manifest-out firmware.manifest
```

When `--app-jump` is used, the tool can wait for the bootloader USB PID to re-enumerate and continue on the new COM port. When `--run --wait-app` is used, it can also wait for the application USB PID after `BOOT_APP`.
The `reboot` command also waits for the bootloader USB PID by default; use `--no-wait-bootloader` to only send the command.

Install the Python serial dependency if needed:

```powershell
py -m pip install pyserial
```

## Integration Notes

An application that runs behind this bootloader should:

- move its flash origin to the target's `APP_BASE`
- reserve the metadata flash region
- keep a normal vector table at the start of the image
- optionally provide a runtime request path that writes the target bootloader magic value and resets the MCU

Targets may implement `boot_platform_watchdog_kick()` if a hardware watchdog remains active while the bootloader runs. The shared core calls this hook during polling, page erase progress, image CRC verification, and deferred reboot/application-jump waits. The STM32G431 and STM32H503 hooks refresh IWDG directly when the peripheral definition is present, which keeps an already-enabled independent watchdog alive without starting one on boards that do not use it.

## License

Affine-authored code is licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE). Vendored third-party files retain their original licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
