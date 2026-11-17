"""Host tests: the bootloader core and the STM32F072 dual-transport target
code compiled for the PC, driven by tools/boot_test.py.

    python3 tests/host/run.py [--build-dir DIR] [--cc gcc]

Needs a host C compiler (gcc or clang; MinGW on Windows), openssl, and
pyserial. Runs on Windows and on Linux (x86-64 for the flash ECC test).

- core_test: the core in-process. Update flows, signed and anti-rollback
  manifests, flash ECC errors, the metadata format, resync and idle timeout,
  retry dedupe, and many whole updates over a lossy channel.
- uart_sim: the core behind a TCP port, as a board on a UART. boot_test.py
  flashes it unsigned, signed and encrypted, over a lossy line, and catches the
  auto-boot window with --wait-hello.
- dual_sim: targets/stm32f072 main.c and transport.c with the real arbiter and
  core (AFFINE_BOOT_TRANSPORT=dual), USB and UART each on a TCP port.
"""
from __future__ import annotations

import argparse
import os
import pathlib
import random
import re
import shutil
import struct
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
TOOL = REPO / "tools" / "boot_test.py"
AES_KEY = "00112233445566778899aabbccddeeff"
TARGET_ID = "0x47343331"
CORE_SOURCES = [REPO / "src" / f"{name}.c" for name in ("bootloader", "boot_protocol", "boot_image", "boot_security", "boot_crypto")]

results: list[tuple[str, bool]] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    results.append((name, ok))
    print(f"{'PASS' if ok else 'FAIL'}  {name}  {detail}".rstrip(), flush=True)


def run(cmd: list, **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kwargs)


def must(cmd: list, **kwargs) -> subprocess.CompletedProcess:
    r = run(cmd, **kwargs)
    if r.returncode != 0:
        sys.exit(f"command failed: {' '.join(map(str, cmd))}\n{r.stdout}{r.stderr}")
    return r


def make_fixtures(fx: pathlib.Path) -> None:
    fx.mkdir(parents=True, exist_ok=True)
    must(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", fx / "key.pem"])
    must([sys.executable, REPO / "tools" / "generate_boot_keys_header.py", "--public-key", fx / "key.pem",
          "--aes-key-hex", AES_KEY, "--out", fx / "keys.h"])
    rng = random.Random(1)
    image = bytearray(rng.getrandbits(8) for _ in range(8192))
    image[0:8] = struct.pack("<II", 0x20008000, 0x08006101)
    (fx / "app.bin").write_bytes(image)
    for name, extra in (("plain", []), ("ar9", ["--anti-rollback", "--security-version", "9"]),
                        ("ar5", ["--anti-rollback", "--security-version", "5"])):
        must([sys.executable, TOOL, "COM0", "flash", fx / "app.bin", "--dry-run", "--target-id", TARGET_ID,
              "--manifest-key", fx / "key.pem", "--manifest-key-id", "2", *extra, "--manifest-out", fx / f"{name}.manifest"])

    # An odd-sized WCH-linked app proves FF is transmitted padding, not blank flash.
    ch32_image = bytearray(rng.getrandbits(8) for _ in range(513))
    ch32_image[:8] = struct.pack("<II", 0x1000006F, 0x00006000)
    ch32_image[16:24] = bytes.fromhex("39e339e3ffffffff")
    (fx / "ch32-odd.bin").write_bytes(ch32_image)
    (fx / "ch32-app.bin").write_bytes(ch32_image + b"\xff")
    for name, extra in (("signed", []), ("encrypted", ["--encrypted"])):
        must([sys.executable, TOOL, "COM0", "flash", fx / "ch32-odd.bin", "--dry-run",
              "--target-id", "0x43323033", "--manifest-key", fx / "key.pem", "--manifest-key-id", "2",
              "--anti-rollback", "--security-version", "9",
              "--manifest-nonce", "000102030405060708090A0B0C0D0E0F",
              *extra, "--manifest-out", fx / f"ch32-{name}.manifest"])


def build(cc: str, out: pathlib.Path, fx: pathlib.Path) -> dict[str, pathlib.Path]:
    exe = ".exe" if os.name == "nt" else ""
    libs = ["-lws2_32"] if os.name == "nt" else []
    common = [cc, "-std=gnu11", "-O1", "-g", "-w", f"-I{HERE}", f"-I{REPO}", f"-I{REPO / 'include'}", f"-I{fx}",
              '-DAFFINE_BOOT_KEYS_OVERRIDE_HEADER="keys.h"']
    if os.name != "nt":
        common += ["-D_GNU_SOURCE"]  # REG_EFL for the Linux ECC signal handler.
    f072 = [f"-I{HERE / 'stub'}", f"-I{REPO / 'targets' / 'stm32f072'}", "-DAFFINE_BOOT_TRANSPORT_DUAL=1"]
    bins = {name: out / f"{name}{exe}" for name in ("core_test", "uart_sim", "dual_sim")}

    must([*common, HERE / "core_test.c", *CORE_SOURCES, *libs, "-o", bins["core_test"]])
    must([*common, HERE / "uart_sim.c", *CORE_SOURCES, *libs, "-o", bins["uart_sim"]])
    must([*common, *f072, "-Dmain=target_main", "-c", REPO / "targets" / "stm32f072" / "main.c", "-o", out / "f072_main.o"])
    must([*common, *f072, HERE / "dual_sim.c", out / "f072_main.o", REPO / "targets" / "stm32f072" / "transport.c",
          REPO / "targets" / "common" / "boot_rx_arbiter.c", *CORE_SOURCES, *libs, "-o", bins["dual_sim"]])
    return bins


class Sim:
    """A simulator process; its output goes to a log, the first line names its ports."""

    def __init__(self, exe: pathlib.Path, logs: pathlib.Path, name: str, args: list):
        self.log_path = logs / f"{name}.log"
        self.log = open(self.log_path, "w")
        self.proc = subprocess.Popen([str(exe), *map(str, args)], stdout=self.log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 5.0
        self.ports: list[int] = []
        while time.monotonic() < deadline and not self.ports:
            m = re.search(r"\[sim\] ports? ([\d ]+)\n", self.log_path.read_text())
            if m:
                self.ports = [int(p) for p in m.group(1).split()]
            else:
                time.sleep(0.02)
        if not self.ports:
            self.stop()
            sys.exit(f"{name}: simulator did not start\n{self.log_path.read_text()}")

    def url(self, channel: int = 0) -> str:
        return f"socket://127.0.0.1:{self.ports[channel]}"

    def stop(self) -> str:
        time.sleep(0.2)
        self.proc.kill()
        self.proc.wait()
        self.log.close()
        return self.log_path.read_text()


def tool(url: str, *args, timeout: float = 240) -> subprocess.CompletedProcess:
    return run([sys.executable, TOOL, url, *args, "--transport", "uart"], cwd=REPO, timeout=timeout)


def uart_tests(exe: pathlib.Path, fx: pathlib.Path, logs: pathlib.Path) -> None:
    app, key = fx / "app.bin", fx / "key.pem"
    signed = ["--manifest-key", key, "--manifest-key-id", "2"]
    # Leave room for consecutive lost replies on the lossy UART cases.
    lossy = ["--timeout", "0.3", "--retries", "12"]
    cases = [
        ("probe", [], ["probe"]),
        ("unsigned flash", [], ["flash", app, "--run"]),
        ("signed flash", [], ["flash", app, *signed, "--run"]),
        ("signed, encrypted, anti-rollback", [],
         ["flash", app, *signed, "--encrypted", "--encrypt-key", AES_KEY, "--anti-rollback", "--security-version", "3", "--run"]),
    ]
    cases += [(f"lossy line, unsigned, seed {s}", [60, 60, 60, 30, 0, s], ["flash", app, *lossy, "--run"]) for s in (1, 2, 3, 4)]
    cases += [(f"lossy line, signed and encrypted, seed {s}", [60, 60, 60, 30, 0, s],
               ["flash", app, *lossy, *signed, "--encrypted", "--encrypt-key", AES_KEY, "--run"]) for s in (11, 12)]
    for name, sim_args, args in cases:
        sim = Sim(exe, logs, "uart_" + re.sub(r"\W+", "_", name), [0, *sim_args])
        r = tool(sim.url(), *args)
        log = sim.stop()
        jumped = "--run" not in map(str, args) or "jump to application" in log
        check(f"uart: {name}", r.returncode == 0 and jumped, "" if r.returncode == 0 else (r.stdout + r.stderr)[-300:])

    # A board in its application, reset every 1.5 s: --wait-hello catches the window.
    sim = Sim(exe, logs, "uart_wait_hello", [0, 0, 0, 0, 0, 1500])
    r = tool(sim.url(), "hello", "--wait-hello", "5")
    log = sim.stop()
    check("uart: --wait-hello catches the auto-boot window", r.returncode == 0 and "caught the auto-boot window" in log)


def last_stats(text: str) -> tuple[int, ...]:
    lines = [line for line in text.splitlines() if ": rx usb=" in line]
    m = re.search(r"answers usb=(\d+) uart=(\d+).*noise=(\d+).*overflow usb=(\d+) uart=(\d+) \| locked=(-?\d+)", lines[-1])
    return tuple(int(x) for x in m.groups())


def dual_tests(exe: pathlib.Path, fx: pathlib.Path, logs: pathlib.Path) -> None:
    app, key = fx / "app.bin", fx / "key.pem"
    flash = ["flash", app, "--timeout", "0.3", "--retries", "8", "--manifest-key", key, "--manifest-key-id", "2", "--run"]

    def sim(name: str, *args) -> Sim:
        return Sim(exe, logs, f"dual_{name}", [0, 0, *args])

    for ch, name in ((0, "usb"), (1, "uart")):
        s = sim(f"alone_{name}")
        r = tool(s.url(ch), *flash)
        t = s.stop()
        st = last_stats(t)
        check(f"dual: signed flash on {name} alone", r.returncode == 0 and "jump to application" in t and st[1 - ch] == 0,
              f"answers usb={st[0]} uart={st[1]}")

    for ch, name in ((0, "usb"), (1, "uart")):
        s = sim(f"noise_{name}", 1 - ch, 30, 0, ch + 5)
        time.sleep(0.5)  # noise alone during the auto-boot window
        r = tool(s.url(ch), *flash)
        t = s.stop()
        st = last_stats(t)
        check(f"dual: flash on {name}, noise on the other", r.returncode == 0 and "jump to application" in t and st[1 - ch] == 0
              and st[2] > 1000, f"noise_bytes={st[2]}")

    s = sim("noise_only", 1, 30, 0, 9)
    time.sleep(2.0)
    r = tool(s.url(0), "hello", "--timeout", "0.3")
    st = last_stats(s.stop())
    check("dual: noise alone does not lock, a host on the other channel still gets in", r.returncode == 0 and st[5] == 0
          and st[1] == 0, f"locked={st[5]}")

    s = sim("loser")
    r1 = tool(s.url(0), "hello", "--timeout", "0.3")
    r2 = tool(s.url(1), "hello", "--timeout", "0.2", "--retries", "2")
    r3 = tool(s.url(0), "status", "--timeout", "0.3")
    st = last_stats(s.stop())
    check("dual: the other channel is ignored once locked", r1.returncode == 0 and r2.returncode != 0 and r3.returncode == 0
          and st[1] == 0)

    s = sim("race")
    procs = [subprocess.Popen([sys.executable, str(TOOL), s.url(c), "flash", str(app), "--timeout", "0.3", "--retries", "2",
                               "--transport", "uart"], cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
             for c in (0, 1)]
    rcs = sorted(p.wait(timeout=120) for p in procs)
    st = last_stats(s.stop())
    check("dual: two hosts at once, exactly one wins", rcs[0] == 0 and rcs[1] != 0 and (st[0] == 0 or st[1] == 0))

    s = sim("reset")
    r1 = tool(s.url(0), "hello", "--timeout", "0.3")
    r2 = tool(s.url(0), "reboot", "--timeout", "0.3")
    time.sleep(0.4)
    r3 = tool(s.url(1), "hello", "--timeout", "0.3")
    st = last_stats(s.stop())
    check("dual: a reset releases the lock", r1.returncode == 0 and r2.returncode == 0 and r3.returncode == 0 and st[5] == 1)

    ok = wrong = 0
    runs = 6
    for seed in range(runs):
        ch = seed % 2
        s = sim(f"lossy_{seed}", 1 - ch, 10, 150, seed + 100)
        r = tool(s.url(ch), "flash", app, "--timeout", "0.2", "--retries", "10", "--run")
        t = s.stop()
        ok += r.returncode == 0 and "jump to application" in t
        wrong += last_stats(t)[1 - ch] != 0
    check(f"dual: lossy channel with noise on the other, {runs} flashes", ok == runs and wrong == 0,
          f"ok={ok} answered_on_wrong_channel={wrong}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=pathlib.Path, default=REPO / "build-host-tests")
    parser.add_argument("--cc", default=os.environ.get("CC") or ("gcc" if shutil.which("gcc") else "cc"))
    parser.add_argument("--pico-sdk-dir", type=pathlib.Path,
                        help="pico-sdk 2.3.1 directory for RP2350 CDC header/fake tests")
    parser.add_argument("--at32-sdk-dir", type=pathlib.Path,
                        help="Also test the AT32 CDC adapter with the external SDK headers (no SDK code runs)")
    args = parser.parse_args()

    out = args.build_dir.resolve()
    shutil.rmtree(out, ignore_errors=True)
    fx, logs = out / "fixtures", out / "logs"
    logs.mkdir(parents=True)

    make_fixtures(fx)
    bins = build(args.cc, out, fx)

    # Actual flash adapter with a NOR model; no SDK or hardware is executed.
    rp_common = [args.cc, "-std=gnu11", "-D_GNU_SOURCE", "-O1", "-Wall", "-Wextra", "-Werror",
                 f"-I{REPO}", f"-I{REPO / 'include'}", f"-I{REPO / 'targets/rp2350'}",
                 "-DAFFINE_RP2350_BOARD_ID=BOOT_BOARD_ID_R235_PICO2"]
    rp_flash = out / ("rp2350_flash_test.exe" if os.name == "nt" else "rp2350_flash_test")
    must([*rp_common, f"-I{HERE / 'rp2350_stub'}", HERE / "rp2350_flash_test.c",
          REPO / "targets/rp2350/flash.c", REPO / "targets/common/boot_target_common.c", "-o", rp_flash])
    r = run([rp_flash])
    (logs / "rp2350_flash_test.log").write_text(r.stdout + r.stderr)
    check("RP2350 flash adapter (host NOR/IRQ fakes)", r.returncode == 0 and "ALL PASS" in r.stdout)
    if args.pico_sdk_dir:
        rp_usb = out / ("rp2350_usb_test.exe" if os.name == "nt" else "rp2350_usb_test")
        must([*rp_common, "-DCFG_TUSB_MCU=OPT_MCU_NONE", "-DTUP_DCD_ENDPOINT_MAX=16", "-isystem",
              args.pico_sdk_dir.resolve() / "lib/tinyusb/src", HERE / "rp2350_usb_test.c",
              REPO / "targets/rp2350/usb.c", REPO / "targets/rp2350/usb_descriptors.c",
              REPO / "targets/common/boot_target_common.c", "-o", rp_usb])
        r = run([rp_usb])
        (logs / "rp2350_usb_test.log").write_text(r.stdout + r.stderr)
        check("RP2350 CDC adapter (host USB fakes, TinyUSB headers only)",
              r.returncode == 0 and "ALL PASS" in r.stdout)

    ch32_common = [args.cc, "-std=gnu11", "-D_GNU_SOURCE", "-O1", "-Wall", "-Wextra", "-Werror",
                   f"-I{REPO}", f"-I{REPO / 'include'}", f"-I{HERE}",
                   f"-I{HERE / 'ch32_stub'}", f"-I{REPO / 'targets/ch32v203'}"]
    ch32_flash = out / ("ch32_flash_test.exe" if os.name == "nt" else "ch32_flash_test")
    must([*ch32_common, f"-I{fx}", '-DAFFINE_BOOT_KEYS_OVERRIDE_HEADER="keys.h"', "-DAFFINE_BOOT_CH32V203=1", "-Wno-int-to-pointer-cast",
          "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
          HERE / "ch32_flash_test.c", REPO / "targets/ch32v203/flash.c",
          REPO / "targets/ch32v203/jump_prepare.c", *CORE_SOURCES,
          REPO / "targets/common/boot_target_common.c", "-o", ch32_flash])
    r = run([ch32_flash, fx])
    (logs / "ch32_flash_test.log").write_text(r.stdout + r.stderr)
    check("CH32 flash, layout, RISC-V entry and jump cleanup (host fakes)",
          r.returncode == 0 and "ALL PASS" in r.stdout)
    ch32_usb = out / ("ch32_usb_test.exe" if os.name == "nt" else "ch32_usb_test")
    must([*ch32_common, HERE / "ch32_usb_test.c", REPO / "targets/ch32v203/usb.c",
          REPO / "targets/common/boot_target_common.c", "-o", ch32_usb])
    r = run([ch32_usb])
    (logs / "ch32_usb_test.log").write_text(r.stdout + r.stderr)
    check("CH32 CDC adapter (host USB fakes)", r.returncode == 0 and "ALL PASS" in r.stdout)

    ch32_detach = out / ("ch32_detach_test.exe" if os.name == "nt" else "ch32_detach_test")
    must([*ch32_common, HERE / "ch32_detach_test.c", REPO / "targets/ch32v203/usb_detach.c",
          "-o", ch32_detach])
    r = run([ch32_detach])
    (logs / "ch32_detach_test.log").write_text(r.stdout + r.stderr)
    check("CH32 USBD/USBFS pull-ups and 50 ms detach (host register fakes)",
          r.returncode == 0 and "ALL PASS" in r.stdout)

    # Source fixtures exercise the guard; both real adapted files are checked in CI.
    import runpy
    startup_check = runpy.run_path(str(REPO / "tools/check_ch32.py"))["check_startup"]
    good_startup = "handle_reset:\nli t0, 0x1888\ncsrw mstatus, t0\njal SystemInit\nmret\n"
    startup_check(good_startup)
    guard_ok = True
    for bad in (good_startup.replace("0x1888", "0x88"),
                good_startup.replace("0x1888", "0x8088"),
                good_startup.replace("jal SystemInit\nmret", "mret\ncsrw mstatus, t0"),
                good_startup.replace("mret", "nop")):
        try:
            startup_check(bad)
        except ValueError:
            continue
        guard_ok = False
    check("CH32 startup requires mstatus=0x1888 before mret (host fixtures)", guard_ok)

    r = run([bins["core_test"], fx], timeout=600)
    (logs / "core_test.log").write_text(r.stdout + r.stderr)
    for line in r.stdout.splitlines():
        if line.startswith(("T", "FAIL", "  ")):
            print(f"      {line}")
    check("core_test", r.returncode == 0 and "ALL PASS" in r.stdout)

    uart_tests(bins["uart_sim"], fx, logs)
    dual_tests(bins["dual_sim"], fx, logs)

    if args.at32_sdk_dir:
        sdk = args.at32_sdk_dir.resolve()
        includes = ["libraries/cmsis/cm4/device_support", "libraries/cmsis/cm4/core_support",
                    "libraries/drivers/inc", "middlewares/usb_drivers/inc", "middlewares/usbd_class/cdc"]
        command = [args.cc, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                   "-DAT32F405CCT7", "-DAFFINE_AT32_USB_HS=0", f"-I{REPO}", f"-I{REPO / 'include'}",
                   f"-I{REPO / 'targets/at32f405'}"]
        for directory in includes:
            command += ["-isystem", str(sdk / directory)]
        at32_test = out / ("at32_usb_test.exe" if os.name == "nt" else "at32_usb_test")
        must([*command, HERE / "at32_usb_test.c", REPO / "targets/at32f405/usb.c",
              REPO / "targets/at32f405/usbd_desc.c", REPO / "targets/common/boot_target_common.c",
              "-o", at32_test])
        r = run([at32_test])
        (logs / "at32_usb_test.log").write_text(r.stdout + r.stderr)
        check("AT32 CDC adapter (host USB fakes, SDK headers only)", r.returncode == 0 and "ALL PASS" in r.stdout)

    failed = [name for name, ok in results if not ok]
    print(f"{len(results) - len(failed)}/{len(results)} passed; logs in {logs}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
