#!/usr/bin/env python3
"""Check CH32 ELF/BIN layout, startup vectors, request reservation and safety symbols."""
import pathlib
import re
import struct
import sys

def require(condition, message):
    if not condition:
        raise ValueError(message)

def check_startup(source):
    """Require MPP=M before the SDK startup's mret into main."""
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    require("handle_reset:" in source, "missing SDK reset handler")
    reset = source.split("handle_reset:", 1)[1]
    writes = list(re.finditer(r"^\s*csrw\s+mstatus\s*,.*$", reset, re.M))
    values = list(re.finditer(
        r"^\s*li\s+t0,\s*(0x[0-9a-fA-F]+|[0-9]+)\s*\n\s*csrw\s+mstatus,\s*t0\b",
        reset, re.M))
    mret = re.search(r"^\s*mret\s*$", reset, re.M)
    require(len(writes) == 1 and len(values) == 1 and mret is not None,
            "unexpected startup mstatus/mret sequence")
    require(int(values[0][1], 0) == 0x1888 and values[0].end() < mret.start(),
            "startup must write mstatus=0x1888 (MPP=M) before mret into main")


def elf_symbols(path):
    data = path.read_bytes()
    require(data[:7] == b"\x7fELF\x01\x01\x01", "expected ELF32 little-endian")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    require(header[2] == 243, "expected RISC-V ELF")
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


def check(directory):
    stem = pathlib.Path(directory) / "bootloader_ch32v203"
    check_startup((pathlib.Path(directory) / "ch32-sdk-adapted/startup.S").read_text(encoding="utf-8"))
    symbols, loads = elf_symbols(stem.with_suffix(".elf"))
    binary = stem.with_suffix(".bin").read_bytes()
    base = 0x00000000
    require(symbols["_start"] == base, "entry not at SDK zero alias")
    require(symbols["_vector_base"] == base + 4, "SDK vector offset changed")
    require(struct.unpack_from("<I", binary, 4)[0] == base, "vector 0 is not _start")
    elf_entry = struct.unpack_from("<I", stem.with_suffix(".elf").read_bytes(), 24)[0]
    require(elf_entry == base, "ELF entry mismatch")
    require(0 < len(binary) <= 24576, "24 KiB BIN limit exceeded")
    require(symbols["__flash_end"] == base + len(binary), "load footprint differs from BIN")
    for address, length in loads:
        require(base <= address < address + length <= base + 24576,
                "ELF load outside boot execution alias")
    require(symbols["__boot_request"] == 0x20004F00, "request reservation mismatch")
    require(0x20000000 <= symbols["_ebss"] <= symbols["_susrstack"] <
            symbols["_eusrstack"] == 0x20004F00, "RAM/stack/request overlap")
    forbidden = ("FLASH_OB", "FLASH_ReadOutProtection", "FLASH_EnableWriteProtection",
                 "FLASH_EraseOptionBytes", "FLASH_ProgramOptionByteData",
                 "FLASH_Unlock_Fast", "FLASH_ROM_WRITE")
    require(not any(name.startswith(forbidden) for name in symbols),
            "unexpected option/protection function linked")
    print(f"PASS CH32: entry={base:#010x}, mtvec table={base + 4:#010x}, "
          f"mstatus=0x1888 (MPP=M), BIN={len(binary)}, headroom={24576 - len(binary)}, "
          f"RAM bss end={symbols['_ebss']:#010x}, "
          f"stack={symbols['_susrstack']:#010x}..{symbols['_eusrstack']:#010x}, "
          f"request={symbols['__boot_request']:#010x}")


if __name__ == "__main__":
    check(sys.argv[1])
