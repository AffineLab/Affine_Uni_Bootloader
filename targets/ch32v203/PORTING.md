# CH32V203 C8 port

This is a CH32V203C8T6 port for QingKe V4B, tested on a WeAct CH32V203C8T6
board (results at the end). Identities remain proposals for release. It uses
external `openwch/ch32v20x` commit
`b375bbbc7dfe8642d6fb7c17e6d3207ee1c3c9c1`; the SDK remains read-only.
CMake requires this Git commit, rejects tracked changes, checks required files,
and fails if an expected SDK adaptation no longer matches.
`tools/fetch_ch32_sdk.sh <build-directory>/ch32-sdk` provides a SHA-pinned fetch.

## Build

Use xPack riscv-none-elf-gcc **15.2.0-1**, CMake, Ninja and Python 3:

```sh
cmake -S . -B build-ch32-usbd -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-none-elf-gcc.cmake \
  -DAFFINE_BUILD_CH32V203=ON \
  -DAFFINE_RISCV_TOOLCHAIN_ROOT=/path/to/xpack-riscv-none-elf-gcc-15.2.0-1/bin \
  -DAFFINE_CH32_SDK_DIR=/path/to/ch32v20x \
  -DAFFINE_BOOT_CH32_USB=usbd -DAFFINE_CH32_HSE_HZ=8000000
cmake --build build-ch32-usbd --target bootloader_ch32v203
python3 tools/check_ch32.py build-ch32-usbd
```

Repeat in `build-ch32-usbfs` with `AFFINE_BOOT_CH32_USB=usbfs`.
`RISCV_NONE_ELF_TOOLCHAIN_ROOT` or `PATH` can select the toolchain instead.
A separate build tree is required from Arm builds. Both variants retain the
shared core's RSA-2048/SHA-256 verification, AES-128-CTR and policy options.
The default policy still permits unsigned updates, as on the other targets.

Compile with `-march=rv32imac_zicsr_zifencei -mabi=ilp32`, GNU C11, `-Os`,
LTO and `-msmall-data-limit=0`. Link with `-march=rv32imc -mabi=ilp32
-nostdlib -lgcc`: xPack has no rv32imac/ilp32 newlib multilib. The only library
is the rv32imc/ilp32 **libgcc**, an ISA subset supported by V4B; the map confirms
this selection. There is no newlib, heap, syscall or CRT dependency.
`runtime.c` supplies memcpy/memset/memcmp/memmove and is compiled without LTO so
late compiler-generated memory operations resolve. No system-wide install is
needed. CI pins the Linux xPack archive version and checks its release `.sha`
with `sha256sum --check`; that Linux download/workflow was not run locally.

## SDK evidence

Paths below are relative to the pinned checkout. SDK facts are distinguished
from Affine policy or assumptions; none is a hardware measurement.

| Value used | SDK source / evidence boundary |
| --- | --- |
| QingKe V4B, 64 KiB flash and 20 KiB SRAM family baseline | Root `README.md`: CH32V203 overview and features (also advertises 144 MHz zero-wait operation). |
| C8 uses **CH32V20x_D6**, not D8/D8W | `EVT/EXAM/SRC/Peripheral/inc/ch32v20x.h` device-selection comment lists CH32V203C8 under D6; `EVT/EXAM/SRC/Startup/startup_ch32v20x_D6.S` header repeats that list. D8 is CH32V203RB and D8W is CH32V208 in `EVT/EXAM/SRC/Ld/Link.ld`. |
| 64 KiB flash, 20 KiB SRAM, SRAM base `0x20000000` | C8 MEMORY block in `EVT/EXAM/SRC/Ld/Link.ld`. The tested WeAct board is a C8T6; no other package pinout is inferred. |
| Execution at the zero alias; flash read/program/erase through `0x08000000` | `EVT/EXAM/SRC/Ld/Link.ld` links at `0x00000000`. `EVT/EXAM/IAP/USB_UART/CH32V20x_APP/Ld/Link.ld` links the app at `0x00005000`; IAP `User/ch32v20x_it.c` branches with `li a6, 0x5000; jr a6`; IAP `User/iap.h` uses `FLASH_Base=0x08005000` for flash access. This port follows that separation with a 24 KiB offset. |
| Standard erase **4 KiB**; program full app pages fast (256 bytes), partial pages and metadata as **16-bit halfwords** | `EVT/EXAM/SRC/Peripheral/src/ch32v20x_flash.c`: FLASH_ErasePage, FLASH_ProgramPage_Fast and FLASH_ProgramHalfWord. The adapter keeps the standard erase, preflights all halfwords and masks/restores IRQs per page/halfword operation. |
| A standard-erased sector can be fast-programmed, mixing full pages and standard tails | `EVT/EXAM/BLE/BackupUpgrade_IAP/APP/peripheral_main.c`: jump_APP calls FLASH_ErasePage per 4096 bytes, then 16 FLASH_ProgramPage_Fast calls. `EVT/EXAM/ETH/ETH_IAP/User/IAP_Task.c`: IAP_EEPROM_ERASE_44k uses standard erase; IAP_EEPROM_WRITE uses fast pages followed by FLASH_ProgramHalfWord for the tail. Both modes use the shared CH32V20x driver without a D6-specific restriction on these operations. This is SDK source evidence; the new mixed path has not yet been timed on this C8. |
| Unlock only MODEKEYR with `0x45670123`, `0xCDEF89AB`; set FLOCK (`0x8000`) after each fast page | Driver FLASH_Unlock_Fast / FLASH_ROM_WRITE show separate KEYR and MODEKEYR sequences. The core already calls FLASH_Unlock, so the port writes only MODEKEYR and uses FLASH_Lock_Fast afterwards. It does not call FLASH_ROM_WRITE, repeat KEYR or set normal LOCK inside the page operation. Re-keying an already-unlocked KEYR is not assumed safe. |
| Check PGERR (`0x04`) and WRPRTERR (`0x10`) as well as FLASH_GetStatus and full-page readback | `EVT/EXAM/SRC/Peripheral/inc/ch32v20x.h` defines FLASH_STATR_PGERR; the driver's FLASH_GetStatus tests only BSY/WRPRTERR. The adapter clears these W1C flags before each page, tests them after the void-returning fast helper, always closes fast mode and checks all 64 readback words. |
| Two independent **4 KiB metadata copies**, at `0x0800E000` and `0x0800F000` | Affine layout choice using the standard erase unit. Each has metadata at offset 0 and security state at `0x200`, satisfying `include/boot_image.h`; a fast 256-byte unit would not contain this record/state. |
| HSE8 x PLL6 = core/AHB/APB2 48 MHz; APB1 24 MHz; USB PLL / 1 = **48 MHz** | `EVT/EXAM/USB/USBFS/DEVICE/SimulateCDC/User/system_ch32v20x.c`: SetSysClockTo48_HSE has an explicit D6/HSE8 comment and APB dividers; USBFS_RCC_Init in its `USB_Device/ch32v20x_usbfs_device.c` and USBD `USBLIB/CONFIG/hw_config.c`: 48 MHz PLL divided by 1. We use the same sequence via RCC APIs, with watchdog reload while waiting. |
| HSE build option, no silent HSI fallback | `AFFINE_CH32_HSE_HZ` defaults to 8 MHz, matching the tested WeAct board's crystal. CMake derives the integer PLL multiplier and rejects combinations that cannot produce 48 MHz with multiplier 2..16. Alternate frequencies are configurable, not electrically validated. HSE failure leaves the device waiting while reloading an inherited watchdog. |
| Flash clock caveat above 120 MHz | `EVT/EXAM/FLASH/FLASH_Program/User/main.c` notes zero-wait and non-zero-wait regions and the need for HCLK / 2 above 120 MHz; FLASH_CTLR SCKMOD defaults flash access to half the system clock. At our 48 MHz no high-speed flash-clock change is required. Root `README.md` advertises zero-wait operation for the family. The **exact C8 zero/non-zero-wait split is not established** by the supplied source; `Datasheet/` contains download links only. We use only nominal 64 KiB, no presumed extended capacity. |
| USBD peripheral clock on APB1, USB_LP_CAN1_RX0 IRQ, internal pull-up; PA11/12 setup | `EVT/EXAM/USB/USBD/SimulateCDC/User/USBLIB/CONFIG/hw_config.c`: Set_USBConfig, USB_Port_Set and interrupt setup. No UART, debug prints, EXTI wake IRQ or low-power clock switch is included in this port. |
| USBFS peripheral on AHB, USBFS IRQ, DMA buffers aligned to 4 bytes | `EVT/EXAM/USB/USBFS/DEVICE/SimulateCDC/User/USB_Device/ch32v20x_usbfs_device.[ch]`: USBFS_RCC_Init, USBFS_Device_Init, USBFS_Device_Endp_Init and endpoint buffers. The tested WeAct connector uses PA11/PA12 (USBD), so USBFS was not reachable there; no alternative USBFS pinout is inferred. |
| Both CDC examples: EP0 64, notification IN `0x81`, bulk OUT `0x02`, bulk IN `0x83`, FS bulk 64 | Both examples' usb_desc.[ch] and endpoint initialization/callbacks. The port authors its own matching CDC descriptors with proposed VID/PID, no fabricated USB serial number (iSerialNumber=0). |
| D6 entry instruction followed by absolute-address vector table; mtvec = vector base OR 3 | `EVT/EXAM/SRC/Startup/startup_ch32v20x_D6.S`: .init _start, .vector _vector_base (63 words), handle_reset configures mtvec. `EVT/EXAM/SRC/Ld/Link.ld` places .init before .vector. |
| Upstream GCC uses software machine-interrupt prologues | Affine adaptation: WCH's `interrupt("WCH-Interrupt-fast")` is unavailable in xPack; generated startup changes CSR 0x804 from 3 (SDK comment: nesting/hardware stack enable) to 0. USB handlers use `interrupt("machine")`. Hardware nesting/stacking is disabled for this bootloader. SDK pipeline/prediction CSR 0xBC0 and vector mode remain as supplied. USBD enumeration, app handover and an app SysTick IRQ work on the tested board after the Machine-mode fix below; USBFS is unverified. |
| Keep bootloader main in Machine mode: write mstatus=`0x1888` before mret | SDK D6 startup writes `0x88` (MPP=U) before mret into main. On the board the main-context `csrw mtvec` then trapped after detach; prepare_sdk.py selects MPP=M. check_ch32.py checks the adapted startup sequence; host fixtures reject the old value. |
| SysTick register layout and polling counter | `EVT/EXAM/SRC/Core/core_riscv.h`: SysTick_Type (CTLR, SR, 64-bit CNT/CMP). `EVT/EXAM/SYSTICK/SYSTICK_Interrupt/User/main.c` uses CTLR=0xF; the port's adaptation uses 0x5 (HCLK counting, no interrupt/reload), CMP=UINT64_MAX and a stable high/low/high read for milliseconds. Automatic handover works on the board; the bootloader's counter timing was not measured precisely. |
| Disable/clear all 8 PFIC interrupt banks; disable 4 fast-vector overrides | `EVT/EXAM/SRC/Core/core_riscv.h`: PFIC_Type IRER/IPRR/VTFADDR arrays and NVIC_DisableIRQ, NVIC_ClearPendingIRQ, NVIC_SetFastIRQ. SysTick is stopped, USB disconnected/reset and RCC reset before setting app mtvec and branching. |
| Reload inherited IWDG with `0xAAAA`; never start or configure it | `EVT/EXAM/SRC/Peripheral/src/ch32v20x_iwdg.c`: IWDG_ReloadCounter; `EVT/EXAM/IWDG/IWDG/User/main.c`. Matches AT32/STM32 reload-only handling, with kicks before each erase/program and in clock/delay loops. Erase/signature time versus an inherited timeout is unmeasured. |
| ECC count = 0 | FLASH_Status / FLASH flags in `EVT/EXAM/SRC/Peripheral/inc/ch32v20x_flash.h` and FLASH_GetStatus in its driver expose busy/protection status, no flash ECC handler. No claim about physical fault tolerance is made. |
| One-shot SRAM request, magic `0x424F4F54` at `0x20004F00` | Affine policy, following AT32; address derived from SDK SRAM end `0x20005000` minus `0x100`. Linker excludes the last 256 bytes from boot stack/BSS initialization. On the tested board the request survives NVIC_SystemReset and is consumed once. Retention across other reset/power conditions is unverified, not an SDK guarantee. |

## Layout and proposed identities

| Region | Flash-access addresses (execution alias where applicable) | Size |
| --- | --- | --- |
| Boot | `0x08000000`-`0x08005FFF`; execution `0x00000000`-`0x00005FFF` | 24 KiB, linker enforced |
| App | `0x08006000`-`0x0800DFFF`; execution `0x00006000`-`0x0000DFFF` | 32 KiB |
| Metadata copy 0 | `0x0800E000`-`0x0800EFFF` | 4 KiB |
| Metadata copy 1 | `0x0800F000`-`0x0800FFFF` | 4 KiB |
| Usable SRAM | `0x20000000`-`0x20004EFF` | 20 KiB minus 256 bytes |
| Boot request reservation | `0x20004F00`-`0x20004FFF` | 256 bytes, NOLOAD |

The boot stack is 4 KiB (`0x20003F00`-`0x20004EFF`), below the request word.
The target reports usable SRAM excluding the reservation. The app must reserve
this area too; C initialization must not clear the request before consumption.
Changing part/capacity/layout requires coordinated edits to board_config.h and
the linker script, plus a new reviewed layout ID. HSE and USB are CMake options.

**All identities below are proposals that need confirmation:**

- Target C203 = `0x43323033`.
- Layout C20A = `0x43323041`.
- Reference board C20R = `0x43323052`, board revision 1.
- USB VID:PID `AFF1:52B5` (the next proposal after AT32 52B3 and RP2350 52B4).

## Application and jump contract

Link an application at **`0x00006000`**, using the SDK D6 startup and linker
ordering: a JAL x0 or C.J at _start, padded to four bytes; .vector follows at
`0x00006004` and .vector[0] must equal _start. Reset code must lie inside the
32 KiB app slot, after the 63-word vector table. There is **no Arm SP/reset
pair** and no VTOR. The protocol/target config and CRC/SHA/metadata reads still use the flash-access
address `0x08006000`. The application startup establishes sp and gp and installs
its own mtvec as usual. Its linker must reserve the request area and keep its
stack at/below `0x20004F00`. The app may choose its own interrupt ABI, provided
its startup programs CSR 0x804 to match it.

Applications linked at **`0x08006000` are rejected**. The supplied SDK demonstrates
zero-alias execution, not execution through the programming/read window. There
is no evidence here to justify accepting both conventions; no hardware test was
performed to establish the other one. Validation requires .vector[0]=`0x00006000`
and prepares entry `0x00006000` and mtvec `0x00006007` together.

`src/boot_image.c` selects CH32 instruction/vector validation only under
AFFINE_BOOT_CH32V203. The existing Arm validation and boot_jump.h are unchanged.
CRC, SHA and signed manifests still cover the complete app image.
The jump validates entry preparation again, masks global interrupts with the
SDK primitive, deinitializes USB, stops/zeros SysTick, disables/clears all PFIC
banks and fast-vector overrides, resets RCC, writes mtvec=`0x00006007`, executes
fences and branches to _start. No C stack access occurs after the branch.
`targets/common/boot_jump_riscv.h` is the RISC-V counterpart; it does not change
sp because the SDK _start establishes the application's stack itself.

## Machine-mode handover and privilege audit

The pinned D6 startup writes mstatus=`0x88`, leaving MPP=U, then executes mret
into main. On the board USB still worked, but the main-context jump trapped on
`csrw mtvec` and hung after detach. The adapted startup therefore loads
**`0x1888` before mret**, selecting MPP=M: bootloader main executes in Machine
mode, where the jump can write mtvec and enter the application's startup CSR
sequence.

The SDK IAP SW_Handler jumps from an interrupt handler, which already executes
in Machine mode. That is a relevant difference from this port's main-context
jump; no claim is made about the SDK author's motivation for that design.
The application still uses its own D6 startup and chooses its own execution
mode. Its startup initially receives Machine mode from the bootloader.

Review of main/SystemInit, jump preparation, IRQ save/restore, the USB callbacks
and the shared core found **no dependency on bootloader main being in User
mode**. The bootloader does not use an ecall-based U-to-M trampoline or deliberately
return to U mode after initialization. GCC USB interrupt handlers already use
the Machine interrupt ABI; the SDK CSR 0x800 masking helpers do not change
privilege mode. PFIC/SysTick access and the final mtvec write remain direct.

`tools/check_ch32.py` now requires one `li t0, 0x1888; csrw mstatus, t0` sequence
in adapted `ch32-sdk-adapted/startup.S`, before mret. It fails closed for missing,
duplicate or changed sequences. Both build variants run this check in CI;
positive/negative host fixtures reject `0x88`, another wrong value, duplicate
mstatus writes and a missing mret. Independent ELF disassembly is retained in
the verification receipts.

## Erase value, padding and rewrite policy

`0xE339` is the SDK's erased halfword read value, not a mask of bits that may
only change from one to zero. The host fake records virgin/programmed state
separately and writes the complete requested halfword after an erase; it does
not AND application bytes with E339.

- Metadata and security-state validity use magic, target/board/layout, format
  and CRC (`src/boot_image.c`). E339-filled records fail those checks. App boot
  also requires valid committed metadata, image CRC, entry validation and any
  required signature; it does not compare an empty slot with FF.
- `src/bootloader.c` erases app pages before DATA and each metadata copy before
  commit. Signed sessions erase only after manifest verification. Successful
  DATA retries are deduplicated and do not program again. Clear-metadata erases
  both copies before restoring the security state.
- The CH32 adapter verifies every erased halfword against E339, then permits
  one program attempt per halfword. A 2560-byte RAM eligibility bitmap starts
  clear and is set only by a successful, verified erase in this boot. A whole
  write is preflighted before its first program. It rejects non-erased readback
  and rewrites, including payloads equal to E339 or FFFF. Program status or
  readback failure consumes that halfword's eligibility; erase/restart the update
  rather than retry a partially programmed chunk in place.
- `tools/boot_test.py::load_image` defaults to explicit FF alignment padding.
  It sends that padding as image data and includes it in image size, CRC, SHA
  and manifest. The adapter programs FF normally; it never skips FF bytes.
  FF is not an erased-value assumption. The core's CRC polynomial/table,
  invalid-copy index `UINT32_MAX` and RSA PKCS#1 FF padding also are not blank
  flash checks. Existing Arm/RP2350 host fakes retain their own FF/NOR behavior.

The CH32 test drives the real core and adapter on E339-filled flash through
unsigned, signed and AES-encrypted anti-rollback updates. Its odd 513-byte host
input is explicitly padded to 514 bytes by the tool; programmed FF bytes and
E339 payloads survive verification. It covers empty metadata/slot rejection,
independent metadata copies, preserved security state, retry deduplication,
non-erased/repeated writes, failed erase verification and failed program readback.

## Fast page programming

Only complete, aligned **256-byte application pages covered by one write call**
use FLASH_ProgramPage_Fast. A call can contain a halfword prefix, full pages and
a halfword tail. Separate partial calls are not combined or buffered across
protocol frames. Metadata/security-state records always use standard halfwords,
including the 368-byte metadata record. Standard 4 KiB erase is unchanged.

The existing whole-call E339/eligibility preflight covers every halfword before
any write. For a full page the adapter first copies the possibly unaligned input
into a 4-byte-aligned 64-word stack buffer, consumes all 128 eligibility bits,
kicks the watchdog, masks IRQs and unlocks only MODEKEYR. It checks FLOCK opened,
calls the SDK fast helper, checks status plus PGERR/WRPRTERR, sets FLOCK and
restores the previous IRQ mask. It then checks every readback word. Errors still
consume the entire page's eligibility; erase/restart the update rather than
rewrite it. Normal KEYR/LOCK remain owned by the core. The new buffer costs
256 bytes of stack while programming; the reserved stack remains 4 KiB.

The vendor helper has busy-wait loops without a timeout or return status. The
adapter adds error-flag/readback checks, not a new timing guarantee. Fast-page
timing was measured on the board (below); power interruption and
inherited-watchdog margins are unmeasured. Host fakes exercise MODEKEYR ordering, FLOCK closure before
IRQ restore, no double programming, unaligned input, partial/mixed pages, page
and sector crossings, whole-page preflight, status errors, late readback
mismatch and the 128-page full-slot path. Core signed/encrypted tests verify
that their final 2-byte tail and all metadata remain standard halfword writes.

## Vendor USB integration and tests

`prepare_sdk.py` creates adapted copies **inside the build directory** and
retains WCH notices. It changes only the SDK startup ABI, USBD configuration/
CDC callback bounds and USBFS UART/control seams. USBD's USB-Driver stack is
compiled directly from the SDK. Its CONFIG usb_prop/usb_pwr/usb_istr files are
adapted or copied into the build tree. USBFS's CDC device stack is adapted from
its example. No TinyUSB, UART driver, debug implementation or hardware tool is
used. Descriptors and both hardware adapters live in this target directory.

The common CDC adapter retains the existing core protocol interface and queue.
OUT packets NAK until consumed; packet lengths are bounded at 64 bytes. IN
frames remain owned until all packets complete, including a ZLP for exact
64-byte multiples. Reset discards old queue/packet state. CDC line coding is
seven bytes on interface zero, with no UART reconfiguration. USB serial numbers
are omitted until a reviewed unique-ID policy is chosen.

```sh
python3 tests/host/run.py
# Also exercise existing external-SDK header tests:
python3 tests/host/run.py --at32-sdk-dir /path/to/at32-sdk --pico-sdk-dir /path/to/pico-sdk
AFFINE_CH32_SDK_DIR=/path/to/ch32v20x bash tools/ci.sh
```

The new host tests execute the actual mixed fast-page/halfword adapter against E339/status
fakes, layout bounds, metadata isolation, malformed JAL/C.J entry rejection,
the shared core's CH32 validator, one-shot request consumption, actual jump
cleanup against fake PFIC/SysTick registers, CDC packet/ZLP/retry/reset
buffering, and both pull-ups being off before the 50 ms detach delay. They do not execute vendor USB control engines, CSR instructions,
clock setup or physical flash operations. `tools/check_ch32.py` checks ELF/BIN
load addresses, vector/entry placement, RAM/stack/request separation and absence
of linked option/protection functions. Linker assertions also enforce the
24 KiB image and 4 KiB stack reservation.

## Board results

WeAct CH32V203C8T6 board (8 MHz HSE, USB-C on PA11/PA12), Windows 11 host,
2026-11-17. Flashed through the ROM ISP with wchisp 0.3.0. The part arrived with
code read protection on; it was removed with `wchisp config unprotect`, which
erases the chip. No other option bytes were changed.

| Check | Result |
| --- | --- |
| Enumeration | USBD build: CDC `AFF1:52B5`; probe, device-info, status, diag and metadata as designed |
| USBFS build | Does not enumerate on this board: its connector is on the USBD pins. USBFS is unverified |
| Startup after ISP or soft reset | Hung in USB bring-up until USBD was reset at init and USB detached before `reboot` (both now in the port); afterwards it enumerates 0.2 s after the wchisp reset and 1.1 s after `reboot` |
| Jump | Hung until main ran in Machine mode (`mstatus` `0x1888`, above). Afterwards an app linked at `0x00006000` with the SDK D6 startup runs, including its SysTick IRQ |
| Auto-boot, re-entry | Jump at the end of the 1 s window; the app's request at `0x20004F00` plus NVIC_SystemReset returns to the bootloader, which stays (one-shot) |
| Full slot (32 KiB) | BEGIN (8 x 4 KiB erases) 0.09 s; DATA 0.44 s with fast pages (3.4 ms per 256-byte frame, 73 KiB/s; standard half-words took 41 s, 2.5 ms each); COMMIT 0.95 s |
| Write edges | 2- to 256-byte chunks, page crossings, duplicates, 64-byte frame boundaries; COMMIT CRC correct; offset gap refused with RANGE |
| Rejections | Wrong target/board/layout: BAD_TARGET; oversized image: IMAGE_TOO_LARGE; odd length: ALIGNMENT |
| Interrupted update | BEGIN plus two DATA frames, then reboot: stays in the bootloader with IMAGE_CRC |
| Security, test-key build | 14/14: signed (key id 2) accepted; foreign key, unknown key id, another image's manifest, encryption without signature and wrong AES key refused; encrypted + anti-rollback version 5 accepted and runs; version 3 refused, also after `clear-metadata`; version 6 accepted. With the default policy an unsigned update is still accepted below the floor |

The wchisp `flash` command erases only the sectors it writes, so the app and
metadata survive a bootloader reinstall; `wchisp erase` clears the whole chip.

Not tested: USBFS on suitable wiring, power loss during erase, program or
commit (fast pages included), cold-start retention of the request word,
inherited-watchdog margins, flash endurance and the exact C8 zero/non-zero-wait
boundary. Release IDs still need confirmation.
