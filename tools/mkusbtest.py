#!/usr/bin/env python3
import argparse
from pathlib import Path

SECTOR = 512

parser = argparse.ArgumentParser()
parser.add_argument('image')
parser.add_argument('--size-mib', type=int, default=16)
args = parser.parse_args()

path = Path(args.image)
size = args.size_mib * 1024 * 1024

create = not path.exists()
if create:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('wb') as f:
        f.truncate(size)

with path.open('r+b') as f:
    first = bytearray(SECTOR)
    first[:24] = b'ROOTOS USB TEST v0.45\0\0\0'
    first[510:512] = b'\x55\xAA'
    f.seek(0)
    f.write(first)

    second = bytearray(SECTOR)
    msg = b'RootOS USB Mass Storage READ(10) test sector 1\n'
    second[:len(msg)] = msg
    for i in range(len(msg), SECTOR):
        second[i] = i & 0xFF
    f.seek(SECTOR)
    f.write(second)

print(f'USB test disk ready: {path} ({size // (1024*1024)} MiB)')
