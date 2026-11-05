#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tongxiaojun
#
"""Inspect a Linux x86 boot-protocol image and its boot_params layout.

Field offsets follow Documentation/arch/x86/boot.rst `struct setup_header`
(starts at zero-page offset 0x1f1) and `struct boot_params`.
"""

import json
import struct
import sys

E820_RAM, E820_RESERVED = 1, 2

# setup_header offsets within boot_params
O_SETUP_SECTS   = 0x1f1
O_BOOT_FLAG     = 0x1fe
O_HEADER        = 0x202
O_VERSION       = 0x206
O_TYPE_LOADER   = 0x210
O_LOADFLAGS     = 0x211
O_CODE32_START  = 0x214
O_RAMDISK_IMG   = 0x218
O_RAMDISK_SIZE  = 0x21c
O_CMDLINE_PTR   = 0x228
O_INITRD_MAX    = 0x22c
O_KERNEL_ALIGN  = 0x230
O_RELOCATABLE   = 0x234
O_MIN_ALIGN     = 0x235
O_XLOADFLAGS    = 0x236
O_CMDLINE_SIZE  = 0x238
O_PREF_ADDRESS  = 0x258
O_INIT_SIZE     = 0x260
O_HANDOVER      = 0x264

# boot_params offsets
O_E820_ENTRIES  = 0x1e8
O_E820_TABLE    = 0x2d0

XLF_KERNEL_64 = 1 << 0


def parse_image(data):
    if len(data) < 0x300 or data[O_BOOT_FLAG:O_BOOT_FLAG + 2] != b"\x55\xaa":
        raise SystemExit("not a boot-protocol image (missing 0xAA55 at 0x1fe)")
    if data[O_HEADER:O_HEADER + 4] != b"HdrS":
        raise SystemExit("missing 'HdrS' header magic at 0x202")
    setup_sects = data[O_SETUP_SECTS] or 4
    payload_off = (setup_sects + 1) * 512
    return {
        "setup_sects": setup_sects,
        "payload_off": payload_off,
        "version": struct.unpack_from("<H", data, O_VERSION)[0],
        "loadflags": data[O_LOADFLAGS],
        "code32_start": struct.unpack_from("<I", data, O_CODE32_START)[0],
        "kernel_alignment": struct.unpack_from("<I", data, O_KERNEL_ALIGN)[0],
        "relocatable": data[O_RELOCATABLE],
        "xloadflags": struct.unpack_from("<H", data, O_XLOADFLAGS)[0],
        "pref_address": struct.unpack_from("<Q", data, O_PREF_ADDRESS)[0],
        "init_size": struct.unpack_from("<I", data, O_INIT_SIZE)[0],
        "cmdline_size": struct.unpack_from("<I", data, O_CMDLINE_SIZE)[0],
        "payload": data[payload_off:],
    }


def build_boot_params(info, *, load_addr, cmdline_addr, ramdisk_addr=0,
                      ramdisk_size=0, mem_mb=256, e820_holes=()):
    bp = bytearray(4096)

    def u8(off, v): bp[off] = v & 0xff
    def u16(off, v): struct.pack_into("<H", bp, off, v)
    def u32(off, v): struct.pack_into("<I", bp, off, v)
    def u64(off, v): struct.pack_into("<Q", bp, off, v)

    u16(O_BOOT_FLAG, 0xAA55)
    bp[O_HEADER:O_HEADER + 4] = b"HdrS"
    u16(O_VERSION, info["version"])
    u8(O_SETUP_SECTS, info["setup_sects"])
    u8(O_TYPE_LOADER, 0xFF)          # undefined loader
    u8(O_LOADFLAGS, info["loadflags"] | 0x80)   # keep LOADED_HIGH, set CAN_USE_HEAP
    u32(O_CODE32_START, load_addr)
    u32(O_CMDLINE_PTR, cmdline_addr)
    u32(O_KERNEL_ALIGN, info["kernel_alignment"])
    u8(O_RELOCATABLE, info["relocatable"])
    u16(O_XLOADFLAGS, info["xloadflags"])
    u32(O_CMDLINE_SIZE, 2048)
    u32(O_INIT_SIZE, info["init_size"])
    if ramdisk_addr:
        u32(O_RAMDISK_IMG, ramdisk_addr)
        u32(O_RAMDISK_SIZE, ramdisk_size)
    u32(O_INITRD_MAX, 0x37FFFFFF)

    entries = [
        (0x0, 0x9FC00, E820_RAM),
        (0x9FC00, 0xA0000 - 0x9FC00, E820_RESERVED),
        (0xF0000, 0x100000 - 0xF0000, E820_RESERVED),
        (0x100000, mem_mb * 1024 * 1024 - 0x100000, E820_RAM),
    ]
    entries.extend(e820_holes)
    entries.sort()
    u8(O_E820_ENTRIES, len(entries))
    off = O_E820_TABLE
    for addr, size, typ in entries:
        struct.pack_into("<QQL", bp, off, addr, size, typ)
        off += 20
    return bytes(bp), entries


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    if not args:
        print(__doc__)
        return 2
    info = parse_image(open(args[0], "rb").read())
    is64 = bool(info["xloadflags"] & XLF_KERNEL_64)
    load = info["code32_start"]
    out = {
        "version": f"0x{info['version']:x}",
        "loadflags": f"0x{info['loadflags']:x}",
        "xloadflags": f"0x{info['xloadflags']:x}",
        "kernel_64": is64,
        "relocatable": info["relocatable"],
        "payload_bytes": f"0x{len(info['payload']):x}",
        "load_addr": f"0x{load:x}",
        "entry": f"0x{load + (0x200 if is64 else 0):x}",
        "init_size": f"0x{info['init_size']:x}",
        "kernel_alignment": f"0x{info['kernel_alignment']:x}",
    }
    print(json.dumps(out, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
