#!/usr/bin/env python3

import argparse
import os
import shutil
import struct
import sys
import tempfile

SECTOR_SIZE = 512
HEADER_LBA = 2048
NEW_MAGIC = b"ROOTFS42"
NEW_VERSION = 2
NEW_NODE_COUNT = 256
NEW_NODE_SLOT_SECTORS = 130
NEW_NODE_AREA_LBA = HEADER_LBA + 1

OLD_MAGIC = b"ROOTFS40"
OLD_VERSION = 1
OLD_NODE_SLOT_SECTORS = 10
OLD_NODE_AREA_LBA = HEADER_LBA + 1

NODE_META_SECTORS = 2
OLD_DATA_SECTORS = 8
NEW_DATA_SECTORS = 128
NODE_MAGIC = b"NODE"

HEADER_STRUCT = struct.Struct("<8sIIIIIII")


def read_header(path: str):
    try:
        with open(path, "rb") as handle:
            handle.seek(HEADER_LBA * SECTOR_SIZE)
            raw = handle.read(SECTOR_SIZE)
    except OSError:
        return None

    if len(raw) < HEADER_STRUCT.size:
        return None

    return HEADER_STRUCT.unpack_from(raw)


def classify(path: str):
    header = read_header(path)

    if header is None:
        return "unknown"

    (
        magic,
        version,
        sector_size,
        node_count,
        node_slot_sectors,
        node_area_lba,
        volume_sectors,
        _flags,
    ) = header

    if sector_size != SECTOR_SIZE or node_count != NEW_NODE_COUNT:
        return "unknown"

    if (
        magic == NEW_MAGIC
        and version == NEW_VERSION
        and node_slot_sectors == NEW_NODE_SLOT_SECTORS
        and node_area_lba == NEW_NODE_AREA_LBA
        and volume_sectors > 0
    ):
        return "rootfs42"

    if (
        magic == OLD_MAGIC
        and version == OLD_VERSION
        and node_slot_sectors == OLD_NODE_SLOT_SECTORS
        and node_area_lba == OLD_NODE_AREA_LBA
        and volume_sectors > 0
    ):
        return "rootfs40"

    return "unknown"


def write_header(handle, total_sectors: int):
    volume_sectors = total_sectors - HEADER_LBA
    header = bytearray(SECTOR_SIZE)

    HEADER_STRUCT.pack_into(
        header,
        0,
        NEW_MAGIC,
        NEW_VERSION,
        SECTOR_SIZE,
        NEW_NODE_COUNT,
        NEW_NODE_SLOT_SECTORS,
        NEW_NODE_AREA_LBA,
        volume_sectors,
        0,
    )

    handle.seek(HEADER_LBA * SECTOR_SIZE)
    handle.write(header)


def create_disk(path: str, size_mib: int, force: bool) -> None:
    if size_mib < 32:
        raise ValueError("ROOTFS42 disk must be at least 32 MiB")

    if os.path.exists(path) and not force:
        disk_type = classify(path)

        if disk_type == "rootfs42":
            print(f"ROOTFS42 disk ready: {path}")
            return

        if disk_type == "rootfs40":
            upgrade_rootfs40(path)
            return

        raise RuntimeError(
            f"refusing to overwrite existing non-RootOS file: {path}\n"
            "use --force only if you really intend to replace it"
        )

    size_bytes = size_mib * 1024 * 1024
    total_sectors = size_bytes // SECTOR_SIZE
    minimum_sectors = (
        NEW_NODE_AREA_LBA
        + NEW_NODE_COUNT * NEW_NODE_SLOT_SECTORS
    )

    if total_sectors <= minimum_sectors:
        raise ValueError("disk is too small for ROOTFS42")

    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)

    with open(path, "wb") as handle:
        handle.truncate(size_bytes)
        write_header(handle, total_sectors)
        handle.flush()
        os.fsync(handle.fileno())

    print(f"Created ROOTFS42 persistent disk: {path}")
    print(f"Size: {size_mib} MiB")
    print("Maximum file size: 64 KiB")


def upgrade_rootfs40(path: str) -> None:
    size_bytes = os.path.getsize(path)
    total_sectors = size_bytes // SECTOR_SIZE
    minimum = NEW_NODE_AREA_LBA + NEW_NODE_COUNT * NEW_NODE_SLOT_SECTORS

    if total_sectors <= minimum:
        raise RuntimeError(
            "existing ROOTFS40 image is too small for ROOTFS42"
        )

    directory = os.path.dirname(path) or "."
    fd, temp_path = tempfile.mkstemp(prefix="rootfs42-", dir=directory)
    os.close(fd)

    backup = path + ".rootfs40.bak"

    try:
        with open(path, "rb") as old, open(temp_path, "wb+") as new:
            new.truncate(size_bytes)
            write_header(new, total_sectors)

            migrated = 0

            for index in range(NEW_NODE_COUNT):
                old_base = OLD_NODE_AREA_LBA + index * OLD_NODE_SLOT_SECTORS
                old.seek(old_base * SECTOR_SIZE)
                metadata = bytearray(old.read(NODE_META_SECTORS * SECTOR_SIZE))

                if len(metadata) != NODE_META_SECTORS * SECTOR_SIZE:
                    raise RuntimeError("short read while migrating metadata")

                if metadata[:4] == b"\0\0\0\0":
                    continue

                if metadata[:4] != NODE_MAGIC or metadata[4] != 1:
                    raise RuntimeError(
                        f"invalid ROOTFS40 node metadata at slot {index}"
                    )

                used = metadata[5]
                size = struct.unpack_from("<I", metadata, 8)[0]

                if used not in (0, 1) or size > OLD_DATA_SECTORS * SECTOR_SIZE:
                    raise RuntimeError(f"invalid ROOTFS40 node {index}")

                metadata[4] = 2

                new_base = NEW_NODE_AREA_LBA + index * NEW_NODE_SLOT_SECTORS
                new.seek(new_base * SECTOR_SIZE)
                new.write(metadata)

                if used and size:
                    old.seek((old_base + NODE_META_SECTORS) * SECTOR_SIZE)
                    payload = old.read(OLD_DATA_SECTORS * SECTOR_SIZE)
                    new.seek((new_base + NODE_META_SECTORS) * SECTOR_SIZE)
                    new.write(payload)

                if used:
                    migrated += 1

            new.flush()
            os.fsync(new.fileno())

        if not os.path.exists(backup):
            shutil.copy2(path, backup)

        os.replace(temp_path, path)
        print(f"Upgraded ROOTFS40 -> ROOTFS42: {path}")
        print(f"Preserved nodes: {migrated}")
        print(f"Backup: {backup}")

    finally:
        if os.path.exists(temp_path):
            os.unlink(temp_path)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create/upgrade the RootOS v0.42 development disk"
    )

    parser.add_argument("path")
    parser.add_argument("--size-mib", type=int, default=64)
    parser.add_argument("--force", action="store_true")

    args = parser.parse_args()

    try:
        create_disk(args.path, args.size_mib, args.force)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"mkrootdisk: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
