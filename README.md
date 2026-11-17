# Affine Uni Bootloader

A USB CDC bootloader for Affine boards, with 24 KiB STM32/AT32/CH32 and 32 KiB RP2350 boot regions (the RP2350 image itself is capped at 28 KiB). It takes firmware over USB CDC (or a USART on STM32F072), checks it with CRC32 and, when required, an RSA-2048 signature, and starts a standard application image with no header in front of it. The core (`src/`, `include/`) is shared; each target in `targets/` supplies flash, transport, clock and jump code.

## Targets

| Target | Board | Bootloader PID | Application slot | Metadata | Transport |
| --- | --- | --- | --- | --- | --- |
| STM32F072 | Monica, Monica Lite R1 | `0x52B2` | `0x08006000`-`0x0801F7FF` (102 KiB) | `0x0801F800`- | USB, UART, or both |
| STM32G431 | Mai / Curva | `0x52B0` | `0x08006000`-`0x0801CFFF` (92 KiB) | `0x0801E000`- | USB |
| AT32F405 xC | Reference (board ID proposed) | `0x52B3` (proposed) | `0x08006000`-`0x0803EFFF` (228 KiB) | `0x0803F000`- | USB FS or HS |
| CH32V203C8 | Reference (all IDs proposed) | `0x52B5` (proposed) | `0x08006000`-`0x0800DFFF` (32 KiB) | `0x0800E000`- (two 4 KiB copies) | USB USBD or USBFS |
| STM32H503 | Linnea | `0x52B1` | `0x08006000`-`0x0801BFFF` (88 KiB) | `0x0801C000`- | USB |
| RP2350 / RP2354 | Pico 2 or RP2354 (`AFFINE_RP2350_BOARD`, all IDs proposed) | `0x52B4` (proposed) | `0x10008000`-`0x101FDFFF` (2008 KiB) | `0x101FE000`- | USB FS |
| ESP32-S3 | WeAct ESP32-S3-N16R8 (all IDs proposed) | `0x52B6` (proposed) | `ota_0` partition, flash `0x120000`-`0x51FFFF` (4 MiB) | `affmeta` partition, `0x110000`- (two 4 KiB copies) | USB OTG (TinyUSB CDC) |

The bootloader occupies `0x08000000`-`0x08005FFF` on STM32/AT32/CH32 and `0x10000000`-`0x10007FFF` on RP2350; USB VID is `0xAFF1` (proposed for RP2350). Target, board and layout IDs are in [include/boot_target_ids.h](include/boot_target_ids.h). `targets/stm32f401` is a memory layout template and `targets/stub` a minimal platform for bring-up.

- **STM32G431**: `0x0801D000`-`0x0801DFFF` is the application's settings page (Mai_stm32) and is never erased. The first flash page holds a boot guard that starts the ST ROM DFU loader if the rest of the bootloader is incomplete; `g431_bl_updater` replaces the bootloader in the field from an application.
- **STM32F072**: Cortex-M0 has no VTOR, so the bootloader copies the application vectors to SRAM and remaps it: applications must start their RAM at `0x200000C0`. Monica Lite R1 builds have the same boot guard.
- **STM32H503**: `-DAFFINE_BOOT_STM32H503_KEYCHIP_LAYOUT=ON` limits the slot to 40 KiB (`0x08006000`-`0x0800FFFF`) for Keychip.

## Building

Needs CMake, Ninja and the Arm GNU toolchain (on `PATH`, or `-DAFFINE_ARM_NONE_EABI_TOOLCHAIN_ROOT=<dir>`). STM32Cube HAL and USB middleware subsets are vendored in `third_party/` ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

```sh
cmake -S . -B build-g431 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-gcc.cmake -DAFFINE_BUILD_STM32G431=ON
ninja -C build-g431 bootloader_stm32g431 g431_bl_updater
```

| Option | Values |
| --- | --- |
| `AFFINE_BUILD_STM32F072`, `_STM32G431`, `_STM32H503`, `_AT32F405`, `_CH32V203` | the target to build (`bootloader_stm32<target>` or `bootloader_at32f405` / `bootloader_ch32v203`) |
| `AFFINE_BOOT_AT32_USB` | `otgfs1` (default, HICK48 with SOF trimming) or `otghs` (on-chip HS PHY, requires HEXT 12 MHz); point `AFFINE_AT32_SDK_DIR` at the external [pinned SDK](targets/at32f405/PORTING.md) |
| `AFFINE_BOOT_TRANSPORT` | `usb` (default); STM32F072 also `uart` (USART1 on PA9/PA10, 115200 8N1, see `targets/stm32f072/board_pins.h`) and `dual`, which listens on both and stays with the first channel that sends a valid frame header until reset |
| `AFFINE_BOOT_F072_BOARD` | `monica` (default) or `monica_lite_r1` |
| `AFFINE_BOOT_KEYS_HEADER` | key header from `tools/generate_boot_keys_header.py`; without it the development keys in `include/boot_keys.h` are used |
| `AFFINE_BOOT_ALLOW_UNSIGNED_UPDATES` and the other policy switches | see [include/boot_policy.h](include/boot_policy.h) |
| `AFFINE_BOOT_KEYCHIP_PRODUCTION_POLICY` | H503 Keychip: signed, anti-rollback updates only; requires `AFFINE_BOOT_KEYS_HEADER` |
| `AFFINE_G431_BL_UPDATER_IMAGE` | a stamped bootloader `.bin` for `g431_bl_updater` to carry instead of this build's |

`tools/release.sh` builds the release set into `dist/release/`.

CH32V203 uses the RISC-V toolchain in a separate build directory:

```sh
cmake -S . -B build-ch32-usbd -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-none-elf-gcc.cmake \
  -DAFFINE_BUILD_CH32V203=ON -DAFFINE_CH32_SDK_DIR=<pinned-sdk> \
  -DAFFINE_BOOT_CH32_USB=usbd -DAFFINE_CH32_HSE_HZ=8000000
cmake --build build-ch32-usbd --target bootloader_ch32v203
```

Use `usbfs` in a second directory for the other peripheral. It needs xPack riscv-none-elf-gcc 15.2.0-1 (on `PATH`, or `RISCV_NONE_ELF_TOOLCHAIN_ROOT` / `AFFINE_RISCV_TOOLCHAIN_ROOT`); no newlib is linked. [CH32 port notes](targets/ch32v203/PORTING.md) have the SDK evidence, the application contract and the board results:

- The bootloader executes at the zero alias `0x00000000`; program its BIN at `0x08000000` (wchisp over the ROM ISP works).
- Applications link at `0x00006000` with the SDK D6 startup; the protocol and flash address of the slot stay `0x08006000`. Apps linked at `0x08006000` are rejected.
- Applications keep SRAM `0x20004F00`-`0x20004FFF` free; writing `0x424F4F54` at `0x20004F00` and resetting is a one-shot request to stay in the bootloader.
- Erased flash reads `0xE339`, not `0xFF`.
- USBD is tested on a WeAct CH32V203C8T6 board; USBFS is untested.

ESP32-S3 is not a bootloader port: the stock ESP-IDF second-stage bootloader stays, and the Uni core runs as the `factory` application (an OTA agent) that writes `ota_0` and selects it with `esp_ota_set_boot_partition`. The addresses it reports are its own cache mapping of the partitions, not flash offsets. Build it with ESP-IDF v6.1.1 (`idf.py -C targets/esp32s3/agent build`); see [ESP32-S3 port notes](targets/esp32s3/PORTING.md) for the partition map, the application contract (`affine_esp_enter_bootloader()`, `esp_ota_mark_app_valid_cancel_rollback()`), recovery and board results. It never enables secure boot, flash encryption or eFuse anti-rollback. It is not yet in `tools/release.sh`.

RP2350 is a standalone secure Arm project: `cmake -S targets/rp2350 -B build-rp2350 -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DPICO_SDK_PATH=<sdk> `-Dpicotool_DIR=<installed-package>`, then `cmake --build build-rp2350`; add `-DAFFINE_RP2350_BOARD=rp2354` for RP2354. It includes an offset test app by default (`AFFINE_RP2350_BUILD_TEST_APP=OFF` disables it). See [RP2350 port notes](targets/rp2350/PORTING.md) for the pinned tools, app contract, watchdog request, E10 UF2 block and board results. This target is not yet in `tools/release.sh`.

## Flashing

`tools/boot_test.py` needs Python 3 with pyserial, and openssl for signing.

```sh
py tools/boot_test.py COM3 flash app.bin --app-jump --run --wait-app   # from a running Mai application
py tools/boot_test.py COM10 flash app.bin --manifest-key key.pem --manifest-key-id 2 --run
py tools/boot_test.py COM10 probe            # hello, device info, status, metadata
py tools/boot_test.py COM7 flash app.bin --transport uart --wait-hello 10 --run
```

Other commands: `hello`, `status`, `diag`, `metadata`, `verify-image`, `clear-metadata`, `reboot`, `app-jump`. `--dry-run` with `--manifest-out` writes a signed manifest without a board. The tool speaks bootloader protocol 1.6 and also flashes 1.5.

## Protocol

Each request is a 20-byte header (`AFFB` magic, opcode, sequence, payload length, payload CRC32, header CRC32) and its payload; the answer has the opcode with `0x8000` set. Structures and opcodes are in [include/boot_protocol.h](include/boot_protocol.h). A host sends one request at a time and on a timeout resends the same frame with the same sequence. The bootloader resynchronises on the magic, drops a partial frame after 100 ms of silence, and answers a repeated `DATA` or `COMMIT` without running it again.

An update is `BEGIN`, then `SET_MANIFEST` for a signed image, `DATA` chunks in order, and `COMMIT`. An unsigned `BEGIN` erases the pages the image covers; a signed one erases nothing until the manifest's signature checks out.

## Applications

- Link STM32/AT32 applications at `0x08006000`, or RP2350 applications at `0x10008000`, with a normal vector table; keep clear of metadata (and on G431 the settings page).
- To enter the bootloader, write `0x424F4F54` to `0x20007F00` (G431, H503), `0x20019700` (AT32F405), or `0x20003F00` (F072) and reset. Otherwise the bootloader waits 1 s for a host, then starts a valid application.
- On RP2350 write the same magic to watchdog `scratch[0]`, then call `watchdog_reboot(0, 0, 0)`; the bootloader consumes it once. No SRAM retention is assumed.
- On ESP32-S3 build a normal IDF application for `ota_0` with `targets/esp32s3/partitions.csv`, link the `affine_esp_boot` component and call `affine_esp_enter_bootloader()` (an RTC request plus selecting `factory`). Confirm each new image with `esp_ota_mark_app_valid_cancel_rollback()`, or IDF rolls back to the agent.
- Committed metadata (two copies on G431, H503 and AT32F405, one on F072) holds the image size, CRC32, version and the signed manifest fields; it is checked again at every boot. After a bootloader upgrade the application has to be flashed again.

## Security

- Unsigned updates are allowed by default. Signed updates carry a manifest with an RSA-2048 PKCS#1 v1.5 signature over the target, board, layout, size, CRC, SHA-256 and versions; `key_id` selects the key.
- `--encrypted` streams the image as AES-128-CTR ciphertext. `--anti-rollback` stores a rollback floor that later updates cannot go below; `clear-metadata` keeps it.
- With the default policy unsigned updates bypass signature and rollback checks, and signed updates without `--anti-rollback` skip the floor. Set `AFFINE_BOOT_REQUIRE_SIGNED_UPDATES` and `AFFINE_BOOT_REQUIRE_ANTI_ROLLBACK_FOR_SIGNED` where that matters.
- The keys in `include/boot_keys.h` are for development. Production builds need their own key header and MCU readout protection.
- On G431 and H503 a flash read that hits an ECC error from an interrupted write is counted by the NMI handler and treated as invalid data, so the board stays in the bootloader.

## Tests

`bash tools/ci.sh` builds the supported targets and variants, checks the 24 KiB STM32/AT32/CH32 or 28 KiB RP2350 image limit (Pico 2 and RP2354 builds), and runs `tests/host/run.py`. Host tests cover core/dual-transport clean and lossy links, AT32/RP2350 CDC adapters against fakes and external SDK headers, RP2350 NOR page writes, and CH32 flash/layout/entry/jump cleanup and CDC packet buffering. `tools/check_rp2350.py <build-dir>` inspects both RP2350 ELF/BIN/UF2 artifacts without hardware. `bash tools/ci_esp32s3.sh` (in an ESP-IDF v6.1.1 shell or the `espressif/idf:v6.1.1` container) builds the ESP32-S3 agent, test app and their test variants and packages signed manifests for the test app.

## License

Apache License 2.0 ([LICENSE](LICENSE)). Vendored third-party files keep their own licenses.
