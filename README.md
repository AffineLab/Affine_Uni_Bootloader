# Affine Unified USB-CDC Bootloader

A target-portable STM32 USB-CDC bootloader with a framed firmware update protocol, metadata-based image validation, and host-side flashing tools.

## Overview

Affine Unified USB-CDC Bootloader separates the reusable bootloader core from MCU-specific platform code. The shared core owns protocol framing, CRC validation, download session state, image metadata, and application handoff. Each target supplies flash operations, USB CDC transport glue, memory layout, and reset/jump behavior.

The project is intended for STM32-based devices that need a compact firmware update path over USB CDC without forcing a private header into the application image.

## Current Targets

| Target | Status |
| --- | --- |
| `STM32G431` | Platform layer, USB CDC transport, linker scripts, and CMake target are present. |
| `STM32H503` | Platform layer, USB CDC transport, linker scripts, and CMake target are present. |
| `STM32F072` | Memory layout template only. |
| `STM32F401` | Memory layout template only. |
| `stub` | Minimal host/stub platform for core bring-up. |

## Repository Layout

```text
include/                  Public bootloader interfaces
src/                      Shared protocol, image, security, and state-machine code
targets/                  MCU-specific platform implementations and linker scripts
cmake/toolchains/         Cross-compilation toolchain files
tools/boot_test.py        Host-side USB CDC smoke test and flashing utility
```

## Protocol Summary

The transport is a USB CDC byte stream. The bootloader protocol adds its own fixed header, payload length, sequence number, payload CRC32, and header CRC32.

Main commands:

- `HELLO`: query protocol version, target ID, application base address, slot size, and maximum chunk size.
- `BEGIN`: start a firmware download session and erase the application and metadata regions.
- `DATA`: write one aligned firmware chunk at the next expected offset.
- `COMMIT`: verify the received image and write committed metadata.
- `ABORT`: cancel the active session.
- `GET_STATUS`: query current state, error code, written size, and rolling CRC.
- `GET_DIAG`: query transport and frame decoder diagnostics.
- `BOOT_APP`: jump to the committed application if it validates.

## Image Layout

Applications remain standard bare-metal images:

- `APP_BASE + 0x00` is the initial stack pointer.
- `APP_BASE + 0x04` is the reset handler.
- No private bootloader header is inserted before the vector table.

The bootloader stores image metadata in a separate flash region near the end of the application slot. On boot, it validates metadata, vector table addresses, and image CRC before jumping to the application.

## Security Model

The security layer is intentionally pluggable. The current default implementation is a no-op and accepts only `flags == 0`.

Reserved extension points include:

- session policy validation in `boot_security_begin()`
- streaming chunk transformation or authentication in `boot_security_transform_chunk()`
- final image verification in `boot_security_verify_image()`
- capability reporting through `boot_security_capabilities()`

This keeps the update pipeline stable while leaving room for signature verification, encrypted streams, and anti-rollback policy.

## Building

This project uses CMake. The STM32 targets expect vendor HAL and USB middleware sources in adjacent project directories referenced by `CMakeLists.txt`.

Example STM32H503 build:

```powershell
cmake -S . -B build-stm32h503 `
  -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-gcc.cmake `
  -DAFFINE_BUILD_STM32H503=ON

ninja -C build-stm32h503 bootloader_stm32h503
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
py tools\boot_test.py COM5 status
py tools\boot_test.py COM5 probe
py tools\boot_test.py COM5 flash firmware.bin --run
```

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

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE).
