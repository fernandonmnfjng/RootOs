#!/usr/bin/env python3
"""Strict Multiboot v1 header/placement validator for RootOS."""
import struct
import sys

MAGIC = 0x1BADB002
VIDEO = 1 << 2
AOUT_KLUDGE = 1 << 16
SEARCH = 8192
FULL_SIZE = 48
SUPPORTED_REQUIRED = 0x00000007  # bits 0,1,2

if len(sys.argv) != 2:
    print(f"usage: {sys.argv[0]} <kernel.elf>", file=sys.stderr)
    raise SystemExit(2)

path = sys.argv[1]
blob = open(path, "rb").read()
window = blob[:SEARCH]
magic_bytes = struct.pack("<I", MAGIC)
magic_offsets = []
pos = 0
while True:
    pos = window.find(magic_bytes, pos)
    if pos < 0:
        break
    magic_offsets.append(pos)
    pos += 1

valid = None
for off in range(0, max(0, len(window) - 11), 4):
    magic, flags, checksum = struct.unpack_from("<III", window, off)
    if magic != MAGIC:
        continue
    if ((magic + flags + checksum) & 0xFFFFFFFF) != 0:
        continue
    valid = (off, flags, checksum)
    break

if valid is None:
    print("RootOS: ERROR: GRUB-compatible Multiboot v1 magic/checksum not found", file=sys.stderr)
    if magic_offsets:
        print(f"RootOS: magic 0x1BADB002 exists at offsets {magic_offsets}", file=sys.stderr)
        print("RootOS: but it is misaligned or its flags/checksum are invalid", file=sys.stderr)
    else:
        # Also search beyond 8 KiB to diagnose linker placement.
        anywhere = blob.find(magic_bytes)
        if anywhere >= SEARCH:
            print(f"RootOS: Multiboot magic is at file offset {anywhere}, beyond 8192 bytes", file=sys.stderr)
            print("RootOS: linker.ld must place KEEP(*(.multiboot)) first", file=sys.stderr)
        else:
            print("RootOS: Multiboot magic is absent from kernel.elf", file=sys.stderr)
    raise SystemExit(1)

off, flags, checksum = valid
if off % 4:
    print(f"RootOS: ERROR: header offset {off} is not 4-byte aligned", file=sys.stderr)
    raise SystemExit(1)
if off + FULL_SIZE > SEARCH:
    print(f"RootOS: ERROR: 48-byte graphics header ends at {off + FULL_SIZE}, beyond byte 8192", file=sys.stderr)
    raise SystemExit(1)
if flags & 0x0000FFF8:
    print(f"RootOS: ERROR: unsupported required Multiboot flags: 0x{flags:08x}", file=sys.stderr)
    raise SystemExit(1)
if not (flags & VIDEO):
    print("RootOS: ERROR: video/framebuffer request flag bit 2 is missing", file=sys.stderr)
    raise SystemExit(1)
if flags & AOUT_KLUDGE:
    print("RootOS: ERROR: a.out kludge is enabled for an ELF kernel", file=sys.stderr)
    raise SystemExit(1)

addr_fields = struct.unpack_from("<IIIII", window, off + 12)
mode_type, width, height, depth = struct.unpack_from("<IIII", window, off + 32)
if any(addr_fields):
    print("RootOS: ERROR: unused address slots 12..28 must be zero when bit16 is clear", file=sys.stderr)
    raise SystemExit(1)
if mode_type != 0:
    print(f"RootOS: ERROR: mode_type={mode_type}; expected linear graphics mode 0", file=sys.stderr)
    raise SystemExit(1)

print(f"RootOS: Multiboot v1 header OK at file offset {off}")
print(f"RootOS: header end={off + FULL_SIZE} < {SEARCH}; flags=0x{flags:08x}; checksum=0x{checksum:08x}")
print(f"RootOS: framebuffer preference={width or 'auto'}x{height or 'auto'}x{depth or 'auto'}")
