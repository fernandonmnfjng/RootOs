#!/usr/bin/env python3

import argparse
import os
import struct
import sys

MAGIC = b"RTPGK001"
VERSION = 1
HEADER_SIZE = 256
ENTRY_STRUCT = struct.Struct("<256sIII")
HEADER_STRUCT = struct.Struct("<8sIIIIIII64s32s96s28s")
PACKAGE_TYPE_APPLICATION = 1
MAX_PACKAGE_SIZE = 64 * 1024


def fixed(text: str, size: int) -> bytes:
    raw = text.encode("utf-8")

    if not raw or len(raw) >= size:
        raise ValueError(f"value too long for fixed field ({size}): {text}")

    if b"\0" in raw:
        raise ValueError("NUL is not allowed")

    return raw + b"\0" * (size - len(raw))


def safe_name(name: str) -> bool:
    return bool(name) and all(
        ch.isalnum() or ch in "-_."
        for ch in name
    )


def safe_relative(path: str) -> bool:
    if not path or path.startswith("/"):
        return False

    parts = path.split("/")
    return all(part not in ("", ".", "..") for part in parts)


def build_app(name: str, version_text: str, elf_path: str, output: str):
    if not safe_name(name):
        raise ValueError("invalid package name")

    entry_name = os.path.basename(elf_path)

    if not safe_relative(entry_name):
        raise ValueError("invalid ELF filename")

    with open(elf_path, "rb") as handle:
        elf_data = handle.read()

    package_toml = (
        "[package]\n"
        f'name = "{name}"\n'
        f'version = "{version_text}"\n'
        'type = "application"\n\n'
        "[application]\n"
        f'executable = "{entry_name}"\n'
    ).encode("utf-8")

    app_toml = (
        "[application]\n"
        f'name = "{name}"\n'
        f'version = "{version_text}"\n'
        f'executable = "{entry_name}"\n'
    ).encode("utf-8")

    files = [
        ("package.toml", package_toml),
        ("app.toml", app_toml),
        (entry_name, elf_data),
    ]

    table_offset = HEADER_SIZE
    table_size = len(files) * ENTRY_STRUCT.size
    data_offset = table_offset + table_size

    entries = []
    payload = bytearray()

    for path, data in files:
        if not safe_relative(path):
            raise ValueError(f"unsafe package path: {path}")

        offset = data_offset + len(payload)
        entries.append(
            ENTRY_STRUCT.pack(
                fixed(path, 256),
                offset,
                len(data),
                0,
            )
        )
        payload.extend(data)

    total_size = data_offset + len(payload)

    if total_size > MAX_PACKAGE_SIZE:
        raise ValueError(
            f"package is {total_size} bytes; v0.42 limit is {MAX_PACKAGE_SIZE}"
        )

    header = HEADER_STRUCT.pack(
        MAGIC,
        VERSION,
        HEADER_SIZE,
        len(files),
        table_offset,
        data_offset,
        total_size,
        PACKAGE_TYPE_APPLICATION,
        fixed(name, 64),
        fixed(version_text, 32),
        fixed(entry_name, 96),
        b"\0" * 28,
    )

    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)

    with open(output, "wb") as handle:
        handle.write(header)
        for entry in entries:
            handle.write(entry)
        handle.write(payload)

    print(f"Built RootOS package: {output}")
    print(f"Package: {name} {version_text}")
    print(f"Entry: {entry_name}")
    print(f"Size: {total_size} bytes")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build RootOS .rtpgk packages")
    sub = parser.add_subparsers(dest="command", required=True)

    build = sub.add_parser("build-app")
    build.add_argument("--name", required=True)
    build.add_argument("--version", required=True)
    build.add_argument("--elf", required=True)
    build.add_argument("--output", required=True)

    args = parser.parse_args()

    try:
        if args.command == "build-app":
            build_app(args.name, args.version, args.elf, args.output)
            return 0
    except (OSError, ValueError) as error:
        print(f"rtpgk: {error}", file=sys.stderr)
        return 1

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
