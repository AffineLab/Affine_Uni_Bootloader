#!/usr/bin/env python3
"""Stamps the boot guard record of a guarded STM32F072 bootloader image.

    python tools/patch_boot_guard.py bootloader_stm32f072.elf bootloader_stm32f072.bin [--split DIR]

The guard (targets/stm32f072/boot_guard_stm32f072.c) checks at every start
that everything behind it is complete. This writes the length and the sum it
checks against into the record at offset 0xC0 of the .bin.

--split also writes the two files an installer loads one after the other:
  <name>.page0.bin   the first flash page (0x08000000), written first
  <name>.rest.bin    everything else (0x08000800)
"""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys

MAGIC = 0x44524147
RECORD_OFFSET = 0xC0
FLASH_BASE = 0x08000000
PAGE = 0x800


def guard_end(elf: str, nm: str) -> int:
    out = subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "_guard_end":
            return int(parts[0], 16)
    raise SystemExit("_guard_end not found: is this a guarded build?")


def body_sum(body: bytes) -> int:
    total = 0
    for (word,) in struct.iter_unpack("<I", body):
        total = (((total << 5) | (total >> 27)) & 0xFFFFFFFF) ^ word
    return total


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("bin")
    ap.add_argument("--nm", default="arm-none-eabi-nm")
    ap.add_argument("--split", metavar="DIR", help="also write the page0 / rest files there")
    ap.add_argument("--verify", nargs=2, metavar=("PAGE0", "REST"),
                    help="check a stamped pair of files instead of stamping; ELF and BIN are not written")
    opts = ap.parse_args()

    if opts.verify:
        image = open(opts.verify[0], "rb").read() + open(opts.verify[1], "rb").read()
        magic, length, expected = struct.unpack_from("<III", image, RECORD_OFFSET)
        start = guard_end(opts.elf, opts.nm) - FLASH_BASE
        whole = open(opts.bin, "rb").read()
        ok = (magic == MAGIC and len(open(opts.verify[0], "rb").read()) == PAGE and length == len(image) - start
              and body_sum(image[start:]) == expected and image == whole)
        print(f"guard verify: body {length} bytes, sum {expected:08X}: {'OK' if ok else 'MISMATCH'}")
        return 0 if ok else 1

    image = bytearray(open(opts.bin, "rb").read())
    while len(image) % 4:
        image.append(0xFF)
    magic, = struct.unpack_from("<I", image, RECORD_OFFSET)
    if magic != MAGIC:
        raise SystemExit(f"no guard record at offset {RECORD_OFFSET:#x}")
    start = guard_end(opts.elf, opts.nm) - FLASH_BASE
    if not RECORD_OFFSET + 16 <= start <= PAGE:
        raise SystemExit(f"guard ends at {start:#x}, outside the first page")
    body = bytes(image[start:])
    struct.pack_into("<III", image, RECORD_OFFSET, MAGIC, len(body), body_sum(body))
    open(opts.bin, "wb").write(image)
    print(f"guard: {start} bytes, body {len(body)} bytes, sum {body_sum(body):08X}, image {len(image)} bytes")

    if opts.split:
        os.makedirs(opts.split, exist_ok=True)
        stem = os.path.splitext(os.path.basename(opts.bin))[0]
        open(os.path.join(opts.split, stem + ".page0.bin"), "wb").write(image[:PAGE])
        open(os.path.join(opts.split, stem + ".rest.bin"), "wb").write(image[PAGE:])
    return 0


if __name__ == "__main__":
    sys.exit(main())
