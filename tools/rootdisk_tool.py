#!/usr/bin/env python3

import argparse
import os
import struct
import sys

SECTOR = 512
HEADER_LBA = 2048
MAGIC = b"ROOTFS42"
VERSION = 2
NODE_COUNT = 256
SLOT_SECTORS = 130
NODE_AREA = HEADER_LBA + 1
META_SECTORS = 2
DATA_SECTORS = 128
FILE_MAX = DATA_SECTORS * SECTOR
PATH_MAX = 512
HEADER = struct.Struct("<8sIIIIIII")


def read_header(handle):
    handle.seek(HEADER_LBA * SECTOR)
    raw = handle.read(SECTOR)
    if len(raw) < HEADER.size:
        raise RuntimeError("short ROOTFS42 header")
    values = HEADER.unpack_from(raw)
    if not (
        values[0] == MAGIC
        and values[1] == VERSION
        and values[2] == SECTOR
        and values[3] == NODE_COUNT
        and values[4] == SLOT_SECTORS
        and values[5] == NODE_AREA
    ):
        raise RuntimeError("disk is not ROOTFS42")


def node_base(index):
    return NODE_AREA + index * SLOT_SECTORS


def read_node(handle, index):
    handle.seek(node_base(index) * SECTOR)
    meta = handle.read(META_SECTORS * SECTOR)
    if len(meta) != META_SECTORS * SECTOR:
        raise RuntimeError("short node metadata")
    if meta[:4] == b"\0\0\0\0":
        return None
    if meta[:4] != b"NODE" or meta[4] != 2:
        raise RuntimeError(f"bad node metadata at {index}")
    used = meta[5]
    node_type = meta[6]
    size = struct.unpack_from("<I", meta, 8)[0]
    path_len = struct.unpack_from("<H", meta, 12)[0]
    if used not in (0, 1) or node_type not in (0, 1):
        raise RuntimeError(f"invalid node {index}")
    if path_len >= PATH_MAX or size > FILE_MAX:
        raise RuntimeError(f"invalid node {index}")
    path = bytes(meta[16:16 + path_len]).decode("utf-8")
    return {"used": bool(used), "type": node_type, "size": size, "path": path}


def find_node(handle, path):
    for i in range(NODE_COUNT):
        node = read_node(handle, i)
        if node and node["used"] and node["path"] == path:
            return i, node
    return None, None


def free_slot(handle):
    for i in range(NODE_COUNT):
        node = read_node(handle, i)
        if node is None or not node["used"]:
            return i
    raise RuntimeError("ROOTFS42 has no free node slots")


def write_node(handle, index, path, node_type, size=0):
    raw_path = path.encode("utf-8")
    if len(raw_path) >= PATH_MAX:
        raise RuntimeError("path too long")
    meta = bytearray(META_SECTORS * SECTOR)
    meta[:4] = b"NODE"
    meta[4] = 2
    meta[5] = 1
    meta[6] = node_type
    struct.pack_into("<I", meta, 8, size)
    struct.pack_into("<H", meta, 12, len(raw_path))
    meta[16:16 + len(raw_path)] = raw_path
    handle.seek(node_base(index) * SECTOR)
    handle.write(meta)


def ensure_dir(handle, path):
    if path == "/":
        index, node = find_node(handle, path)
        if node:
            if node["type"] != 0:
                raise RuntimeError("/ is not a directory")
            return index
        index = free_slot(handle)
        write_node(handle, index, "/", 0)
        return index

    if not path.startswith("/"):
        raise RuntimeError("directory path must be absolute")

    current = ""
    ensure_dir(handle, "/")

    for part in [p for p in path.split("/") if p]:
        current += "/" + part
        index, node = find_node(handle, current)
        if node:
            if node["type"] != 0:
                raise RuntimeError(f"not a directory: {current}")
            continue
        index = free_slot(handle)
        write_node(handle, index, current, 0)


def inject(handle, source, destination):
    if not destination.startswith("/") or destination == "/":
        raise RuntimeError("destination must be an absolute file path")
    with open(source, "rb") as src:
        data = src.read()
    if len(data) > FILE_MAX:
        raise RuntimeError(f"file exceeds ROOTFS42 64 KiB limit: {len(data)}")
    parent = destination.rsplit("/", 1)[0] or "/"
    ensure_dir(handle, parent)
    index, node = find_node(handle, destination)
    if node and node["type"] != 1:
        raise RuntimeError("destination exists and is not a file")
    if index is None:
        index = free_slot(handle)
    data_start = (node_base(index) + META_SECTORS) * SECTOR
    handle.seek(data_start)
    handle.write(data)
    remaining = FILE_MAX - len(data)
    # Do not rewrite 64 KiB of zeroes unnecessarily; truncate stale tail sector-wise
    tail = (-len(data)) % SECTOR
    if tail:
        handle.write(b"\0" * tail)
    write_node(handle, index, destination, 1, len(data))
    print(f"Injected {source} -> {destination} ({len(data)} bytes)")


def main():
    parser = argparse.ArgumentParser(description="Manipulate ROOTFS42 development images")
    parser.add_argument("disk")
    parser.add_argument("--inject", nargs=2, metavar=("SOURCE", "DEST"))
    args = parser.parse_args()
    try:
        with open(args.disk, "r+b") as handle:
            read_header(handle)
            if args.inject:
                inject(handle, args.inject[0], args.inject[1])
                handle.flush()
                os.fsync(handle.fileno())
    except (OSError, RuntimeError, UnicodeError) as error:
        print(f"rootdisk_tool: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
