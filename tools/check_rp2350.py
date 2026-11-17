#!/usr/bin/env python3
"""Inspect built ELF/BIN/UF2 artifacts, without loading a board or third-party modules."""
import argparse
import hashlib
import pathlib
import struct


def require(condition, message):
    if not condition:
        raise ValueError(message)


def elf_symbols(path):
    data = path.read_bytes()
    require(data[:7] == b"\x7fELF\x01\x01\x01", "expected ELF32 little-endian")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    require(header[2] == 40, "expected Arm ELF")
    sections = [struct.unpack_from("<10I", data, header[6] + i * header[11])
                for i in range(header[12])]
    symbols = {}
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, value, _, _, _, _ = struct.unpack_from("<IIIBBH", data, offset)
            symbols[names[name:].split(b"\0", 1)[0].decode()] = value
    loads = []
    for i in range(header[10]):
        segment = struct.unpack_from("<8I", data, header[5] + i * header[9])
        if segment[0] == 1 and segment[4]:  # PT_LOAD with file contents
            loads.append((segment[3], segment[4]))
    return symbols, loads


def check(stem, base, end, abs_block):
    symbols, loads = elf_symbols(stem.with_suffix(".elf"))
    binary = stem.with_suffix(".bin").read_bytes()
    require(symbols["__vectors"] == base, f"{stem}: vectors not at image base")
    require(symbols["__flash_binary_start"] == base, "incorrect flash origin")
    require(symbols["__flash_binary_end"] == base + len(binary) <= end, "flash footprint mismatch")
    require(base <= symbols["embedded_block"] < symbols["__embedded_block_end"] <= base + 4096,
            "IMAGE_DEF outside first 4 KiB")
    marker = struct.unpack_from("<I", binary, symbols["embedded_block"] - base)[0]
    require(marker == 0xFFFFDED3, "missing picobin start marker")
    stack, entry = struct.unpack_from("<II", binary)
    require(stack == symbols["__StackTop"] and 0x20000000 < stack <= 0x20082000,
            "invalid initial stack")
    require(entry & 1 and (entry & ~1) == (symbols["_reset_handler"] & ~1), "incorrect reset vector")
    require(base <= (entry & ~1) < end, "reset vector outside slot")
    for address, length in loads:
        require(base <= address < address + length <= end, "ELF load outside flash slot")
    require(not any(name.startswith("otp_") or name.startswith("rom_otp") for name in symbols),
            "unexpected linked OTP function")

    uf2 = stem.with_suffix(".uf2").read_bytes()
    require(len(uf2) % 512 == 0, "truncated UF2")
    pages = {}
    count = len(uf2) // 512 - 1
    for index in range(count + 1):
        block = uf2[index * 512:(index + 1) * 512]
        m0, m1, flags, address, length, number, total, family = struct.unpack_from("<8I", block)
        require((m0, m1, struct.unpack_from("<I", block, 508)[0]) ==
                (0x0A324655, 0x9E5D5157, 0x0AB16F30), "UF2 magic mismatch")
        require(length == 256, "unexpected UF2 payload")
        if index == 0:
            require((flags, address, number, total, family) ==
                    (0xA000, abs_block, 0, 2, 0xE48BFF57), "incorrect E10 absolute block")
            require(block[32:288] == bytes([0xEF]) * 256 and
                    struct.unpack_from("<I", block, 288)[0] == 0x9957E304,
                    "missing RP2_IGNORE_BLOCK extension")
        else:
            require((flags, number, total, family) == (0x2000, index - 1, count, 0xE48BFF59),
                    "incorrect secure Arm UF2 header")
            require(base <= address < address + length <= end and address % 256 == 0,
                    "UF2 payload outside flash slot")
            require(address not in pages, "duplicate UF2 page")
            pages[address] = block[32:288]
    payload = b"".join(pages[address] for address in sorted(pages))
    require(sorted(pages) == list(range(base, base + len(payload), 256)), "noncontiguous UF2")
    require(payload[:len(binary)] == binary, "UF2 differs from BIN")
    print(f"PASS {stem.name}: BIN={len(binary)} UF2={len(uf2)} vectors=0x{base:08X} "
          f"IMAGE_DEF=0x{symbols['embedded_block']:08X} SP=0x{stack:08X}")
    for suffix in (".bin", ".uf2", ".elf"):
        artifact = stem.with_suffix(suffix)
        print(f"  {artifact.name}: {artifact.stat().st_size} bytes SHA256={hashlib.sha256(artifact.read_bytes()).hexdigest()}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=pathlib.Path)
    args = parser.parse_args()
    # The bootloader keeps its last sector free for its relocated E10 block.
    check(args.build_dir / "bootloader_rp2350", 0x10000000, 0x10007000, 0x10007F00)
    check(args.build_dir / "test_app/rp2350_test_app", 0x10008000, 0x101FE000, 0x10FFFF00)


if __name__ == "__main__":
    main()
