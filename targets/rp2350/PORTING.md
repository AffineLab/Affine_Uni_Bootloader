# RP2350 secure Arm reference port

Standalone CMake project, `PICO_PLATFORM=rp2350-arm-s`, `PICO_BOARD=pico2`.
No RISC-V, Non-secure execution, core-1 launch, OTP access, hardware secure boot,
partition-table provisioning, or bootloader self-update is implemented.
All identities are **proposals**: target R235 `0x52323335`, layout R23A
`0x52323341`, Pico 2 board R23P `0x52323350`, revision 1, USB `AFF1:52B4`.

## Dependencies and build

- pico-sdk 2.3.1: `079c6f39023649b154152db30f1d781e884879bc`.
- Its TinyUSB submodule: `86ad6e56c1700e85f1c5678607a762cfe3aa2f47`.
- picotool 2.3.1: `2041936441b48a3cc53ae3da9e805229fe8f4e18`.

SDKs stay external and read-only. Configuration checks version macros and Git
commits (including TinyUSB when using a Git SDK), and requires an installed
picotool CMake package; it never silently fetches a missing tool. CI explicitly
uses `tools/fetch_pico_sdk.sh <dir>` (only the TinyUSB submodule) and
`tools/fetch_picotool.sh <dir> <sdk>` (pinned host build, `PICOTOOL_NO_LIBUSB=1`).
No signing/encryption/OTP command is invoked in the build.

```sh
cmake -S targets/rp2350 -B build-rp2350 -G Ninja \
  -DCMAKE_BUILD_TYPE=MinSizeRel -DPICO_SDK_PATH=/path/to/pico-sdk \
  -Dpicotool_DIR=/path/to/picotool-install/picotool
cmake --build build-rp2350
python3 tools/check_rp2350.py build-rp2350
python3 tests/host/run.py --build-dir build-host \
  --pico-sdk-dir /path/to/pico-sdk --at32-sdk-dir /path/to/at32-sdk
```

Arm GCC must be on PATH (or use the SDK's `PICO_TOOLCHAIN_PATH`). A picotool
built with MinGW also needs the MinGW `bin` directory on PATH for its runtime DLLs.
Shared `AFFINE_BOOT_*` policy switches and `AFFINE_BOOT_KEYS_HEADER` are supported.
Development keys/unsigned updates are the default, as on existing targets.

## Measured layout

Before fixing the layout, a MinSizeRel Arm GCC 13.3.1 skeleton with the SDK
runtime, this TinyUSB CDC adapter and all shared core sources measured **23,620
bytes BIN**, **48,128 bytes UF2**, `size`: text 27,716 / data 0 / BSS 12,152.
Flash/jump entry points were measurement-only stubs; no app address was fixed.
Rounded to six 4 KiB sectors (24 KiB), plus two sectors headroom gives **32 KiB**.
The completed port measured **26,528 bytes BIN**, leaving **6,240 bytes**.
These are build measurements, not board measurements.

| Region | Inclusive addresses | Size |
| --- | --- | --- |
| Boot | `0x10000000` - `0x10007FFF` | 32 KiB |
| App | `0x10008000` - `0x101FDFFF` | 2008 KiB |
| Metadata 0 | `0x101FE000` - `0x101FEFFF` | 4 KiB |
| Metadata 1 | `0x101FF000` - `0x101FFFFF` | 4 KiB |

The same 2 MiB contract applies to RP2354 package flash and Pico 2's 4 MiB flash.
The extra Pico 2 capacity is outside the protocol layout. Linker regions enforce
boot/app bounds. Core writes remain 4-byte aligned; erase units are 4096 bytes.

## Value sources and decisions 4-9

Paths in this table are relative to the pinned pico-sdk unless stated otherwise.

| Value or decision | Evidence / implementation |
| --- | --- |
| Flash base `0x10000000` | `src/rp2_common/pico_standard_link/pico_flash_region.template.ld:1`; 2 MiB capacity is the selected common layout, not a probe of fitted flash. |
| SRAM `0x20000000`, 520 KiB | `src/rp2350/pico_platform/script_include/default_locations.ld:1-6`: 512 KiB main RAM plus two 4 KiB scratch banks. Boot reserves both scratch banks for an 8 KiB core-0 stack via SDK linker variables; core 1 is unused. |
| XOSC 12 MHz, system 150 MHz, USB 48 MHz | `src/rp2350/hardware_regs/include/hardware/platform_defs.h:96-139`; SDK runtime owns clock setup. |
| USB FS, EP0/bulk 64 bytes | TinyUSB `src/portable/raspberrypi/rp2040/dcd_rp2040.c:303` reports full speed. This port selects FS in `tusb_config.h`; CDC EP OUT 01, IN 81, notification 82/8 bytes are descriptor choices. No stdio USB competes for CDC. |
| **4. Page program = 256, sector = 4096** | `src/rp2_common/hardware_flash/include/hardware/flash.h:45-46,91-114` specifies alignment and NOR one-to-zero programming. Each affected page is an SRAM buffer, FF except the new bytes. Core DATA is sequential, 4-byte aligned; metadata is 368 bytes, then separate 32-byte security state at offset 0x200 (`include/boot_image.h:105,139`, `src/bootloader.c:340-342,391-406,590-612` in this repo). Clearing metadata erases then rewrites only state. Later short writes to an existing page preserve earlier bytes; no read/modify/erase cycle is used. Erase/program readback failures return `BOOT_ERR_FLASH`. |
| **5. XIP-safe SDK wrappers** | `src/rp2_common/hardware_flash/flash.c:214-267` places wrappers in RAM, exits XIP, flushes cache, restores XIP. `flash.h:17-31` requires IRQ masking and exclusion of a second executing core. This port masks only one sector/page call, restores the prior mask, runs TinyUSB tasks without recursively decoding commands, and kicks its watchdog between operations. |
| Watchdog 8000 ms | Port policy, below the RP2350 maximum 16777 ms (`src/rp2_common/hardware_watchdog/include/hardware/watchdog.h:80-82`). Boot owns the reload value through `watchdog_enable(8000, true)`; it disables the watchdog before app entry. It never assumes the SDK's RAM reload variable survived an application. |
| **6. M33 handoff** | `src/rp2_common/pico_crt0/crt0.S:424-427` clears MSPLIM on debugger entry; the reset handler assumes bootrom established limits (`:475-476`). Port deinitializes TinyUSB, holds USBCTRL in reset, stops SysTick and both timers' four alarms, disables/clears both NVIC banks, clears PendSV/SysTick pending and MSPLIM/PSPLIM, sets VTOR, barriers, then uses `boot_jump_with_stack`. Register definitions: `src/rp2350/hardware_structs/include/hardware/structs/{nvic,scb,systick,timer}.h`. |
| **6. Clock/runtime restart** | `src/rp2_common/pico_runtime_init/runtime_init_clocks.c:44-61` disables resuscitation and switches sys/ref away from auxiliary sources before PLL initialization; it restarts ticks at `:145-147`. `runtime_init.c:57-87,145-156` resets peripherals without disturbing XIP/live PLLs and subsequently unresets them, including USB. `src/rp2_common/hardware_irq/irq.c:704-721` resets priorities and enables interrupts. Thus clocks are left running at SDK defaults; USB is explicitly reset because early runtime resets exclude it. App must retain normal SDK initialization. This is source proof, not jump validation on silicon. |
| **7. Request = watchdog scratch[0]** | `src/rp2350/hardware_regs/include/hardware/regs/watchdog.h:111-119` specifies retention through soft reset. `hardware_watchdog/watchdog.c:92-108` uses 4-7; CRT0 `:438-451` uses 2-7 for RAM-image boot. Datasheet sections 5.2.4 / 5.2.4.1 (printed p342-343) describe bootrom's 4-7 vector and 2-3 parameters; scratch 0 is outside these reservations. The request is consumed/cleared once, including when its value is not magic. Use watchdog soft reset; RUN/power or chip-level reset does not guarantee retention (datasheet 12.9.5). |
| **8. Vectors first, IMAGE_DEF after** | `src/rp2_common/pico_standard_link/script_include/section_default_text.incl:26-33` retains vectors, binary-info header, embedded block in that order. `src/rp2_common/pico_crt0/embedded_start_block.inc.S:85-95` emits IMAGE_DEF. The app overrides `pico_flash_region.ld` through `pico_add_linker_script_override_path` (`pico_standard_link/CMakeLists.txt:96-127`), setting origin `0x10008000` and length `0x1F6000`; changing flash size alone cannot relocate it. Built ELF/BIN confirm vectors at `0x10008000`, IMAGE_DEF at `0x10008138`, initial SP `0x20082000`. |
| **9. picotool / E10 / family** | `src/cmake/on_device.cmake:79-81,110-111` permits `PICO_NO_PICOTOOL` for ordinary ELF/BIN but disables UF2/postprocessing. This target requires the installed tool and UF2 output. `tools/CMakeLists.txt:615-654` adds `--abs-block` with Pico 2's A2 support and passes the secure Arm family. `src/common/boot_uf2_headers/include/boot/uf2.h:33-38` defines Arm-S `0xE48BFF59`, absolute `0xE48BFF57`. Pinned **picotool** `elf2uf2/elf2uf2.cpp:130-170` prepends an EF-filled absolute block, block 0 of 2, with `RP2_IGNORE_BLOCK` extension; `main.cpp:679` defaults its address to `0x10FFFF00`. No custom converter is used. |

Datasheet used offline: RP2350 Datasheet, local `rp2350.pdf`, SHA256
`a638d6780da919162660691bfa02c6dcf7c2a0b6e9f18e2d7519c80b664a2e39`.
Its Appendix D.2.1 (printed p1351) confirms that the E10 workaround writes the
last flash block on A2 and can overwrite user data; A3+ honors the ignore-block
extension. **BOOTSEL UF2 recovery on A2 can erase metadata at the end of flash.**
Treat it as recovery/reprovisioning, not a metadata-preserving update. Do not use
the test-app UF2 to test the bootloader's commit/jump flow; use its BIN below.

## Application contract and test app

Secure Arm SDK app, vectors at `0x10008000`, normal SDK runtime enabled, flash
ending before `0x101FE000`. See `test_app/link/pico_flash_region.ld` for the
region override. Preserve IMAGE_DEF and normal CRT0/linker sections. The boot
request needs no reserved SRAM location:

```c
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "hardware/structs/watchdog.h"
watchdog_hw->scratch[0] = 0x424F4F54;
__dsb();
watchdog_reboot(0, 0, 0);
```

`rp2350_test_app` is built by default with the bootloader project. It waits about
2 seconds, requests bootloader entry, and resets. Its BIN must be installed
through `tools/boot_test.py COMx flash build-rp2350/test_app/rp2350_test_app.bin
--run`, so committed metadata exists. It then returns to the bootloader and stays
there after the request is consumed; an explicit run can repeat the cycle.

## Untested board checklist

1. Confirm Pico 2 / RP2354 wiring, flash part, silicon revision and SWD/BOOTSEL
   recovery. Load only the boot image at `0x10000000`; account for A2's UF2
   metadata erase above. Do not provision OTP or enable hardware secure boot.
2. Confirm CDC `AFF1:52B4`; run `probe` / `diag`, checking proposed IDs, 4-byte
   writes, 4096-byte erases, app/metadata bounds and one-second host window.
3. Flash the test-app BIN through the protocol, then verify automatic and
   explicit jump, app interrupts/timers/clocks, two-second reset and one-shot
   scratch request. Repeat cold boot, watchdog reset, RUN and BOOTSEL recovery.
4. Exercise short 4-byte and page-crossing writes, 64-byte packet/ZLP boundaries,
   532-byte frames, retries, suspend and reconnect; check diagnostics and USB
   progress during a full-slot erase. Measure worst sector/signature timing
   against the 8-second watchdog and host retry timeout.
5. Validate unsigned/signed/encrypted/rollback flows with reviewed test keys,
   both metadata copies, state preservation on clear, boot-region protection,
   and interrupted erase/program/commit. Check RP2354's fitted flash repeated
   partial-page programming as well as the Pico 2's flash.

Host fakes execute port code but not SDK drivers/MMIO; ELF/UF2 inspection proves
layout/format only. USB enumeration, flash endurance/power-loss behavior,
watchdog timing, jump/re-entry and RP2354 compatibility all require board tests.
