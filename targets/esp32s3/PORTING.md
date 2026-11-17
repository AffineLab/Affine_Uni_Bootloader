# ESP32-S3 OTA agent (WeAct N16R8)

This target uses ESP-IDF **v6.1.1**, the stock IDF second-stage bootloader,
and a factory application running the shared Uni core. The user application
lives in `ota_0`. Native USB (GPIO19/20) carries only Uni CDC protocol frames;
UART0 at 115200 baud carries all logs through the board's USB-UART connector.
The agent uses 16 MiB flash in DIO mode at 40 MHz, with PSRAM disabled.

## Build

From the repository root in an IDF v6.1.1 PowerShell environment:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif\tools'
. C:\Espressif\esp-idf-v6.1.1\export.ps1
idf.py -C targets/esp32s3/agent build size
idf.py -C targets/esp32s3/test_app build size
```

If PowerShell execution policy blocks the export script, start PowerShell with
`-NoProfile -ExecutionPolicy Bypass` for that process. No global policy change
is needed. An installed tool environment can also be supplied process-locally.

The agent pins managed component `espressif/esp_tinyusb` **3.0.1** and retains
`agent/dependencies.lock`; its TinyUSB sources are included in that component.
Keep `managed_components` ignored. The initial agent dependency fetch needs
registry access; subsequent builds can use the verified local managed sources.
The test app has **no managed dependencies and no TinyUSB dependency**.

`tools/ci_esp32s3.sh` sources `$IDF_PATH/export.sh` on Linux, builds both
projects, prints IDF sizes and exact BIN lengths, rejects partition overflow,
and packages the real test-app BIN in three offline manifest smoke checks
(signed; signed with Uni rollback; signed/encrypted with Uni rollback).
Those checks generate an ephemeral RSA key; they do not validate the device's
configured public key or contact any serial port. The dedicated CI job uses
`espressif/idf:v6.1.1`; the existing targets' job is unchanged.

The deployable agent artifacts are under `agent/build`:
`bootloader/bootloader.bin`, `partition_table/partition-table.bin`,
`ota_data_initial.bin`, `affine_esp32s3_agent.bin`, and `flasher_args.json`.
The uploadable user image is `test_app/build/affine_esp32s3_test_app.bin`.
Although IDF prints a factory flash recipe after building the test app, use
that BIN with Uni at `ota_0`; the test project's flash recipe would replace
the factory agent.

## Physical partition map

All ranges below are physical flash offsets; ends are exclusive.

| Region | Offset | Size | End |
| --- | --- | --- | --- |
| IDF second-stage bootloader | `0x000000` | below partition table | `0x008000` |
| Partition table | `0x008000` | `0x1000` | `0x009000` |
| nvs | `0x009000` | `0x4000` | `0x00D000` |
| otadata | `0x00D000` | `0x2000` | `0x00F000` |
| alignment gap | `0x00F000` | `0x1000` | `0x010000` |
| factory (agent) | `0x010000` | `0x100000` | `0x110000` |
| affmeta | `0x110000` | `0x10000` | `0x120000` |
| ota_0 (user app) | `0x120000` | `0x400000` | `0x520000` |
| unused/reserved | `0x520000` | `0xAE0000` | `0x1000000` |

Only affmeta's first two 4 KiB sectors hold Uni metadata/security state.
The full 64 KiB partition provides MMU alignment. The Uni rollback floor is
stored redundantly at offset `0x200` within each metadata sector, using the
shared core's existing rules. It is separate from IDF rollback state.

The startup code creates **one DATA mapping** for physical
`[0x110000, 0x520000)`. Device-info `flash_base` and `metadata_base` report
that mapping's virtual base, and `app_base` is virtual base + `0x10000`.
Reported `flash_size=0x410000` describes this accessible window, rather than
the module's entire 16 MiB. `metadata_size=0x2000`, `app_size=0x400000`,
`erase_size=4096`, `write_size=4`, and `max_chunk_size=256`.
SRAM bounds are zero/unused: the ESP image header replaces the ARM vector check.
These virtual addresses are agent-local, not offsets to pass to esptool or
addresses at which to link an ESP application. Metadata binds the virtual app
address; changing the agent's mapping through an agent update may require
reinstalling the user image. Ordinary resets use the same agent and allocation.
`spi_flash_mmap` places the window in the first free 64 KiB MMU page after the
agent's own code and rodata pages. In this build code ends at `0x42018CD8` and
rodata at `0x3C02A640`, so the window starts at `0x3C030000`. An agent that
grows by more than about 23 KiB of rodata or 29 KiB of code moves it one page up;
the installed application then reads as invalid (it stays in the agent) until
it is flashed again.

The platform requires a factory runtime and validates partition addresses and
sizes before mapping. Erase/program translates virtual addresses and accepts
only the physical interval `[0x110000, 0x520000)`, with overflow-safe bounds,
an explicit unlock and write alignment checks. An erase covers every 4 KiB
sector the requested range touches: the core passes its last unit at the
image's length, not a whole sector. It verifies erased words
and compares programmed bytes through the DATA mapping. It cannot erase the
bootloader, table, otadata or factory through these Uni flash primitives.
Only the IDF boot-selection API changes otadata.

## IDF source decisions

Reference paths are relative to the installed ESP-IDF v6.1.1 source tree,
or the pinned managed component as marked. No IDF v5 API layout is assumed.

| Decision | Source |
| --- | --- |
| Contiguous DATA mapping | `components/spi_flash/include/spi_flash_mmap.h`: `spi_flash_mmap` / `SPI_FLASH_MMAP_DATA`; `components/spi_flash/flash_mmap.c`. The mapping remains allocated for the life of the agent. |
| Fresh mapped reads after erase/write | `components/spi_flash/esp_flash_api.c`: erase and write finish through `flash_end_flush_cache`; `memspi_host_driver.c`: `memspi_host_flush_cache`; `flash_mmap.c`: `spi_flash_check_and_flush_cache` invalidates mapped pages on S3. The platform also checks readback. |
| ESP header validation | `components/bootloader_support/include/esp_app_format.h`: magic `0xE9`, chip ID `ESP_CHIP_ID_ESP32S3`, segment count 1..`ESP_IMAGE_MAX_SEGMENTS` (16). Full ESP validation happens at boot selection. |
| Boot validation and factory selection | `components/app_update/esp_ota_ops.c`: `esp_ota_set_boot_partition` calls `image_validate(..., ESP_IMAGE_VERIFY)` first. Factory selection erases otadata; OTA selection rewrites its state. |
| Rollback state / app confirmation | `components/app_update/include/esp_ota_ops.h` and `esp_ota_ops.c`: `esp_ota_get_state_partition`, `esp_ota_mark_app_valid_cancel_rollback`; `components/bootloader_support/src/bootloader_utility.c` handles pending/aborted images. |
| RTC reservation | `components/esp_system/ld/esp32s3/memory.ld.in` defines RTC slow memory `[0x50000000,0x50002000)`; `sections.ld.in` defines `_rtc_force_slow_end`. Our additional linker script reserves the final eight bytes as NOLOAD and asserts ordinary RTC data ends before it. |
| Milliseconds, watchdog, restart | `components/esp_timer/include/esp_timer.h`: `esp_timer_get_time`; `components/esp_system/include/esp_task_wdt.h`: status/add/reset; `esp_system.h`: `esp_restart` and reset reasons. Main subscribes to the initialized task WDT and also yields regularly for watched idle tasks during erase/CRC/SHA. |
| CDC and serial descriptor | Managed `espressif__esp_tinyusb` 3.0.1: `include/tinyusb.h`, `include/tinyusb_default_config.h`, `include/tinyusb_cdc_acm.h`, `tinyusb_cdc_acm.c`, `usb_descriptors.c`, `Kconfig`. Default descriptors use our VID/PID/strings and the base MAC as twelve uppercase hex digits. RX callbacks run in the TinyUSB task and only drain bytes into an 8 KiB FreeRTOS stream buffer. One main task owns the core and TX queue. |

## Proposed identities and protocol

Confirm all identities before release:

- Target ES3S: `0x45533353`.
- WeAct N16R8 board ES3W: `0x45533357`, revision 1.
- Layout ES3A: `0x45533341`.
- Native CDC VID:PID: `AFF1:52B6`.
- Manufacturer `Affine`, product `Affine Uni Bootloader ES3S`, serial from MAC.

The five shared core files implement the unchanged Uni protocol, manifest v3,
RSA-2048 signature, SHA-256, AES-128-CTR stream decryption, retry handling and
Uni metadata rollback floor. Build policy remains the shared defaults (including
optional signatures and optional Uni anti-rollback). This target does not turn
optional authentication into an IDF secure-boot trust chain. Replace shared
development keys using `-D AFFINE_BOOT_KEYS_HEADER=<generated-header>` and the
core's established key provisioning scheme before
relying on authentication in a product.

`tools/boot_test.py` adds only the target defaults entry. Its BEGIN timeout is
**60 s**: erasing the full 4 MiB slot (1024 sectors) took 28 s on the board when
every sector held data, 6 s when they were blank. Transfers run at about
60 KB/s (154 KB test app: 3.9 s end to end; 4 MiB: 70 s of DATA).

The UART logs reset reason, requested/forced entry, ota_0 state and state-query
error, BEGIN/COMMIT/BOOT_APP status/error, and IDF boot-selection result.
The release agent sends nothing but protocol frames over native USB.

### Single-cable test build

On the WeAct board the two USB-C sockets sit too close for two cables. A test
build of the agent then mirrors the logs to a second CDC interface and adds a
button-free way back into ROM download mode:

```powershell
$t = (Resolve-Path targets/esp32s3).Path.Replace('','/')
idf.py -C targets/esp32s3/agent -B "$t/agent/build-logcdc" `
  -D "SDKCONFIG=$t/agent/build-logcdc/sdkconfig" `
  -D "SDKCONFIG_DEFAULTS=$t/sdkconfig.defaults;$t/agent/sdkconfig.defaults;$t/agent/sdkconfig.logcdc" build
```

Add `-D AFFINE_BOOT_KEYS_HEADER=<header>` (from
`tools/generate_boot_keys_header.py`) for signed tests with a test key.
`CONFIG_AFFINE_AGENT_LOG_CDC` (in `sdkconfig.logcdc`, with
`CONFIG_TINYUSB_CDC_COUNT=2`) gives:

- Interface 0 (`MI_00`): the Uni protocol, unchanged. Use this port with
  `boot_test.py`; both ports share `AFF1:52B6`.
- Interface 2 (`MI_02`): the logs, also still on UART0. Lines wait in a 4 KiB
  ring until a host has held the port open for 200 ms (pyserial sets DTR and
  RTS in separate requests, and each line-state change can flush TinyUSB's TX
  FIFO), so the boot lines are not lost.
- Opening the log port at 1200 baud reboots into ROM download mode on
  USB-Serial-JTAG (`303A:1001`). TinyUSB routes the internal PHY to OTG through
  `RTC_CNTL_USB_CONF`, which survives `esp_restart()`; the hook disconnects,
  routes the PHY back to USB-Serial-JTAG and only then sets
  `RTC_CNTL_FORCE_DOWNLOAD_BOOT`. Without that the ROM has no USB and the host
  keeps a dead `AFF1:52B6` device.

`tools/ci_esp32s3.sh` builds this variant and fails if the release agent has it
enabled.
GET_DIAG counts RX queue overflow and TX queue drops; TX completion means that
TinyUSB accepted/drained the queued bytes, not a host acknowledgment.

## Boot, re-entry and rollback

With blank otadata, IDF starts factory. The agent waits one second for a Uni
frame before attempting the shared verified auto-boot. A consumed RTC request,
or ota_0 state ABORTED/INVALID, suppresses auto-boot. Explicit BOOT_APP remains
allowed, verifies the shared metadata/image/security rules, and then schedules
`esp_ota_set_boot_partition(ota_0)` plus restart. IDF verifies the complete ESP
image before selecting it. If selection fails, the agent logs the IDF error,
sets `BOOT_ERR_NO_VALID_APP` / ERROR for the next GET_STATUS, and stays available;
the deferred selection is not retried in a loop. The initial BOOT_APP response
precedes this deferred IDF check, as required by the existing platform ABI.

The application must confirm its first successful start with
`esp_ota_mark_app_valid_cancel_rollback()`. If it resets before confirmation,
IDF marks it aborted and falls back to factory. The agent's state check prevents
an automatic restart loop. This is otadata rollback, not eFuse anti-rollback.

To implement application command `APP_CMD_JUMP_TO_BOOTLOADER` (`0x25`), add
`targets/esp32s3/components` to `EXTRA_COMPONENT_DIRS`, require
`affine_esp_boot` in the app's component, include `affine_esp_boot.h`, and call
`affine_esp_enter_bootloader()`. It writes a magic and its complement into RTC
slow memory at **`0x50001FF8` / `0x50001FFC`**, validates/selects factory,
and restarts. On selection error it clears the request and returns an
`esp_err_t`. The agent reads and clears both words once. On the board the
request survived every software reset, and a power cycle (RTC memory
undefined) read as no request.

Both images must link `components/affine_esp_boot/rtc_request.ld`; linking the
component does this automatically, even if the app never calls the helper
(the component is linked with WHOLE_ARCHIVE). Its fixed NOLOAD section gives the mailbox
RTC_NOINIT semantics independent of either image's ordinary RTC variable order.
It reserves exactly eight bytes and fails the link if other RTC data overlaps.
Do not use those bytes for ULP or other RTC data in an integrating application.

The normal test app prints its version on UART0, confirms validity, waits three
seconds, and requests factory re-entry. It has no USB transport or USB PID.
For a separate deliberately resetting build (before confirmation):

```powershell
$target = (Resolve-Path targets/esp32s3).Path.Replace('\','/')
idf.py -C targets/esp32s3/test_app -B "$target/test_app/build-crash" `
  -D "SDKCONFIG=$target/test_app/build-crash/sdkconfig" `
  -D "SDKCONFIG_DEFAULTS=$target/sdkconfig.defaults;$target/test_app/sdkconfig.crash" build
```

Upload that variant's `affine_esp32s3_test_app.bin` through Uni for the rollback
test. An explicit BOOT_APP can retry it; do not expect it to confirm itself.

## First installation and recovery

These are hardware procedures, not part of the build checks.
Either port can install the agent. Over the **USB-UART port**, `idf.py` resets
the chip into download mode itself:

```powershell
idf.py -C targets/esp32s3/agent -p <UART-port> flash
```

Over the **native port**, enter ROM download mode by hand (hold BOOT, tap RST,
release BOOT; the port enumerates as `303A:1001`), or with the 1200-baud touch
of a test build, then flash with a watchdog reset. A reset through RTS over
USB-Serial-JTAG leaves the chip in download mode:

```powershell
python -m esptool --chip esp32s3 -p <303A:1001 port> --before no-reset --after watchdog-reset `
  write-flash --flash-mode dio --flash-size 16MB --flash-freq 40m `
  0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin `
  0xd000 build/ota_data_initial.bin 0x10000 build/affine_esp32s3_agent.bin
```

This uses physical offsets `0x0` (IDF bootloader), `0x8000` (table), `0xD000`
(initial otadata) and `0x10000` (factory agent), as recorded in its generated
`flasher_args.json`. Then connect native USB to run Uni host operations:

```powershell
python tools/boot_test.py <native-CDC-port> flash targets/esp32s3/test_app/build/affine_esp32s3_test_app.bin --run
```

For last-resort recovery, enter ROM download mode (either port, as above) and
erase **only otadata** to select factory again:

```text
esptool --chip esp32s3 --port <UART-port> erase_region 0xD000 0x2000
```

Current esptool also spells that subcommand `erase-region`. This preserves
factory, affmeta and ota_0. A valid committed app can auto-boot after the one
second window; connect a host promptly when recovering it. If factory itself
is damaged, reinstall the complete agent artifact set via ROM download mode.

Secure boot, flash encryption and `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK` are
explicitly disabled. The shared component rejects builds enabling them. No
eFuse operation is part of this target's workflow.

## Board results

WeAct ESP32-S3-N16R8, 2026-11-17: ESP32-S3 (QFN56) rev v0.2, 16 MiB quad flash
(JEDEC 20 4018), 8 MiB PSRAM unused, ESP-IDF v6.1.1. Only the native port was
connected (one cable fits), so tests ran on the single-cable test build with a
generated test RSA/AES key; the release agent was checked afterwards.

| Check | Result |
| --- | --- |
| Install | `erase-flash` 2.4 s; agent written over the native port in ROM download mode; boots as CDC `AFF1:52B6` |
| Probe, device-info | Target `ES3S`, board `ES3W` rev 1, layout `ES3A`; flash/metadata base `0x3C030000`, app base `0x3C040000`, size `0x410000`; erase 4096, write 4, chunk 256; capabilities `0xF`. Blank: `NO_VALID_APP`, stays in the agent |
| Unsigned flash and run | 154,192 B committed, BOOT_APP, the test app ran and returned through the RTC request (`request=1 forced=1`): 3.9 s (test build), 4.2 s (release build) |
| Security, test-key build | 10/10: signed key ids 2 and 1 accepted; foreign key and unknown key id 7 refused at SET_MANIFEST (`SIGNATURE`, nothing erased); encryption without signature refused by the host; wrong AES key refused at COMMIT (`IMAGE_CRC`); encrypted + anti-rollback version 5 accepted and runs; version 3 refused (`ROLLBACK`), also after `clear-metadata`; version 6 accepted and runs; floor 6 kept |
| App rollback | Test app that resets before confirming: IDF marks ota_0 ABORTED and starts factory; the agent logs `state=4 forced=1` and stays (no boot loop, > 12 s); a good image plus BOOT_APP recovers |
| Image IDF rejects | One byte flipped in the body: Uni CRC and VERIFY_IMAGE pass, BOOT_APP's `esp_ota_set_boot_partition` returns `ESP_ERR_OTA_VALIDATE_FAILED`; status `NO_VALID_APP`, agent stays |
| Recovery | ROM download mode, `esptool erase-region 0xD000 0x2000`, watchdog reset: factory starts, auto-boots the committed app |
| Power cycle | No host: the agent auto-boots the app after the 1 s window (RTC contents after power-on read as no request); the app returns |
| Full 4 MiB slot | BEGIN 6.0 s over blank sectors, 28.0 s over written ones; 4 MiB transfer 70 s (about 60 KB/s) |
| Release build | Single CDC interface, no test hooks: device-info, unsigned flash and run, auto-boot as above |

Found and fixed on the board: an erase of the image's last, partial unit was
refused (now whole sectors); the agent slept a 10 ms tick between frames (now
blocks on the RX buffer: 12.9 s to 3.9 s for the test app); the agent project
ignored `-D SDKCONFIG_DEFAULTS`.

Not verified: the CI job (`tools/ci_esp32s3.sh` on `espressif/idf:v6.1.1`) has
not run yet; the dual-cable UART log path was not exercised on this board.

