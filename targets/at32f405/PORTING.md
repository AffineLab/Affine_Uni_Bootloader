# AT32F405 xC bring-up

This target uses the external Artery AT32F402/405 firmware library v2.1.5,
commit `5dd9d55a2ce9ffa8fe0cb2652ac142920f2094a3`:

```sh
cmake -S . -B build-a405 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-gcc.cmake \
  -DAFFINE_BUILD_AT32F405=ON \
  -DAFFINE_AT32_SDK_DIR=/path/to/AT32F402_405_Firmware_Library \
  -DAFFINE_BOOT_AT32_USB=otgfs1
ninja -C build-a405 bootloader_at32f405
```

Use a separate build directory with `AFFINE_BOOT_AT32_USB=otghs` for the
on-chip HS PHY. The SDK remains outside the repository and is not modified.
CMake checks its files, version macros and (for Git checkouts) commit.
`tools/fetch_at32_sdk.sh <build-directory>/at32-sdk` performs CI's shallow,
SHA-pinned fetch. A supplied `AFFINE_AT32_SDK_DIR` is read without fetching.

## Hardware decisions and SDK evidence

All paths below are relative to the pinned SDK. No reference manual was
available for this port; these are SDK-derived values, not hardware measurements.

| Value used | SDK source |
| --- | --- |
| Cortex-M4F, hard-float `fpv4-sp-d16`; xC selection `AT32F405CCT7` | `libraries/cmsis/cm4/device_support/at32f402_405.h`: device selection, `__CORTEX_M`, `__FPU_PRESENT` |
| Flash `0x08000000`, 256 KiB; SRAM `0x20000000`, 102 KiB, end `0x20019800` | `libraries/cmsis/cm4/device_support/startup/gcc/linker/AT32F405xC_FLASH.ld`: MEMORY and `_estack` |
| Erase sector 2048 bytes on xC (128 sectors) | `project/at_start_f405/examples/flash/flash_write_read/src/flash.c`: `AT32F402_405xC` / `SECTOR_SIZE` |
| Program 32-bit words, alignment 4 | `libraries/drivers/src/at32f402_405_flash.c`: `flash_word_program()`; halfword and byte operations also exist |
| No flash ECC counter, return 0 | `libraries/drivers/inc/at32f402_405_flash.h`: FLASH flags/register definitions; `flash_operation_status_get()` in the driver reports busy/program/protection errors without ECC |
| Independent WDT reload `0xAAAA`; bootloader does not start WDT | `libraries/drivers/inc/at32f402_405_wdt.h`: `WDT_CMD_RELOAD`; corresponding driver `wdt_counter_reload()` |
| FS: HICK48 for USB with USB SOF trimming; core/AHB/APB2 216 MHz, APB1 108 MHz via HICK8 * 108 / 4 | `project/at_start_f405/examples/acc/calibration/src/at32f402_405_clock.c`; `libraries/drivers/src/at32f402_405_crm.c`: HICK selection and PLL functions |
| FS ACC thresholds 7980 / 8000 / 8020, HICKTRIM mode | `project/at_start_f405/examples/usb_device/virtual_comport/src/main.c`: `usb_clock48m_select(USB_CLK_HICK)` |
| HS: HEXT 12 MHz required; PLL * 72 / 4 gives core 216 MHz, PLLU / 18 gives USB 48 MHz, PHY input HEXT / 1 | `project/at_start_f405/examples/usb_device/virtual_comport/inc/at32f402_405_conf.h` and `src/at32f402_405_clock.c`; `libraries/drivers/inc/at32f402_405_crm.h`: `CRM_USB_PHY12_CLOCK_HEXT_DIV_1` is the only PHY12 selection |
| LDO 1.3 V, six flash wait cycles for 216 MHz | Both clock examples above |
| FS1 = OTG1, IRQ OTGFS1; HS = OTG2, IRQ OTGHS, dedicated on-chip PHY | `project/at_start_f405/examples/usb_device/two_otg_device_demo/inc/usb_conf.h` and `src/main.c`; `middlewares/usb_drivers/src/usbd_core.c`: HS PHY detection/init |
| CDC bulk FS 64 bytes, HS 512 bytes; EP IN 0x81, OUT 0x01, notification 0x82 | `middlewares/usbd_class/cdc/cdc_class.h`; speed-dependent endpoint opening in `cdc_class.c` |
| FS FIFO 1280 bytes; HS FIFO 4096 bytes available; allocations in words | `project/at_start_f405/examples/usb_device/virtual_comport/inc/usb_conf.h` |
| USB data pins owned by USB; ignore VBUS/ID, no low-power USB or DMA | GPIO setup and `USB_VBUS_IGNORE` in the USB examples |
| FS-only unused HS PHY power workaround | `libraries/cmsis/cm4/device_support/system_at32f402_405.c`: `reduce_power_consumption()` and its PLL/HEXT precondition (FAQ0148) |

The layout uses the SDK's 102 KiB SRAM map. SRAM parity does not change it:
an erased user system data byte leaves parity off (`USD_RAM_PRT_CHK_DISABLE`
is bit 0x80 set), and the SDK's `sram/sram_parity_check` example, which turns
parity on, still maps 102 KiB. `flash_ssb_status_get()` reads the setting.

## Layout and identities

- Boot: `0x08000000`–`0x08005FFF` (24 KiB), enforced by the linker.
- App: `0x08006000`–`0x0803EFFF` (228 KiB).
- Metadata: two independent 2 KiB sectors at `0x0803F000` and `0x0803F800`.
- Boot request: word `0x424F4F54` at `0x20019700` (SRAM end minus `0x100`);
  the linker reserves the last `0x100` bytes from the bootloader's RAM.
- Target `A405` = `0x41343035`; layout `A40A` = `0x41343041`.
- **Proposals requiring owner confirmation:** board `A40R` = `0x41343052`,
  revision 1; USB VID:PID `AFF1:52B3`. Both USB builds share these identities.

The adapter uses the SDK's device stack and CDC endpoint definitions with
Affine-authored descriptors and CDC callbacks. RX is packet-based with OUT
NAKing until the main loop consumes the data; the existing core reassembles
frames up to 532 bytes. TX keeps each core frame alive through completion,
including a terminating ZLP for an exact multiple of the negotiated packet
size. Control requests are bounded before passing buffers to the SDK.

Jump follows the G431 sequence: mask interrupts, disconnect/reset USB and
ACC, reset clocks, stop SysTick, disable/clear NVIC interrupts, set VTOR,
then call `boot_jump_with_stack()`. There is no boot guard or updater.

## Board test checklist (not performed)

1. Confirm the part is an AT32F405 xC, supplies and USB wiring match the SDK, and the HS build has a 12 MHz HEXT oscillator
   and the required OTGHS_R connection. Keep SWD/recovery access available.
2. Flash only the selected bootloader at `0x08000000`. With no committed
   application, verify CDC enumeration as `AFF1:52B3`; inspect FS/HS speed
   and endpoint sizes, including an FS host/hub with the HS build.
3. Run `py tools/boot_test.py COMx probe`, then `diag`. Confirm target,
   board, layout, 4-byte programming, 2 KiB erase units and metadata addresses.
4. Link an application at `0x08006000`, with stack below `0x20019700`, and
   reserve the boot request word in the application's linker script too.
   Flash a small unsigned image, verify it, then test signed, encrypted and
   anti-rollback flows with reviewed test keys. Do not use development keys
   for production releases.
5. Check short/final chunks, USB-packet boundary crossings, exact 64/512-byte
   transfers, the largest 532-byte frame, retries and unplug/reconnect. Check
   diagnostic queue drops/overflows and repeat over both USB ports.
6. Verify both metadata copies and preservation of the bootloader region.
   Interrupt power during app erase/write and metadata commit; confirm an
   invalid image stays in the bootloader and a surviving valid copy is used.
7. Confirm one-second automatic boot, explicit run, application interrupt
   handling after the jump, USB reinitialization, reset, and entry via
   `0x424F4F54` at `0x20019700`. Repeat with an already-running WDT, using a
   timeout long enough for erase and signature verification.
8. Repeat cold starts and USB attach/detach across expected supply and
   temperature conditions to validate HICK/SOF trimming. Confirm HS operation
   on the intended cable/host and board PHY wiring.

Host fakes test the adapter's descriptors, FS/HS packet buffering, full-size
frames, ZLP completion, reset/suspend and control bounds. They do not execute
SDK code, initialize clocks or demonstrate USB or flash hardware behavior.
