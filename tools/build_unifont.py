#!/usr/bin/env python3

import sys
import struct


BMP_COUNT = 0x10000

GLYPH_HEIGHT = 16

RECORD_SIZE = 33

REPLACEMENT = 0xFFFD


def empty_glyph():
    return bytearray(RECORD_SIZE)


def parse_bitmap(hex_string):
    """
    GNU Unifont:

    32 hex chars:
        8x16

    64 hex chars:
        16x16

    Algunas versiones pueden contener glifos
    más anchos; por ahora RootOS acepta 8 y 16.
    """

    raw = bytes.fromhex(hex_string.strip())


    if len(raw) == 16:
        width = 8

        bitmap = bytearray(32)

        for y in range(16):
            bitmap[y * 2] = raw[y]
            bitmap[y * 2 + 1] = 0

        return width, bitmap


    if len(raw) == 32:
        width = 16

        return width, bytearray(raw)


    return None


def main():

    if len(sys.argv) != 3:
        print(
            "usage: build_unifont.py "
            "<unifont.hex> <output.bin>"
        )

        return 1


    input_file = sys.argv[1]

    output_file = sys.argv[2]


    """
    Cada codepoint BMP ocupa exactamente:

        1 byte  width
        32      bitmap

    Total:

        65536 * 33
        ~= 2.1 MiB
    """

    font = bytearray(
        BMP_COUNT
        *
        RECORD_SIZE
    )


    glyph_count = 0


    with open(
        input_file,
        "r",
        encoding="ascii",
        errors="ignore"
    ) as source:

        for line in source:

            line = line.strip()


            if not line:
                continue


            if ":" not in line:
                continue


            code_text, bitmap_text = (
                line.split(":", 1)
            )


            try:
                codepoint = int(
                    code_text,
                    16
                )

            except ValueError:
                continue


            """
            En esta primera versión guardamos
            el Basic Multilingual Plane.
            """

            if not (
                0
                <=
                codepoint
                <
                BMP_COUNT
            ):
                continue


            parsed = parse_bitmap(
                bitmap_text
            )


            if parsed is None:
                continue


            width, bitmap = parsed


            offset = (
                codepoint
                *
                RECORD_SIZE
            )


            font[offset] = width


            font[
                offset + 1:
                offset + 33
            ] = bitmap


            glyph_count += 1


    with open(
        output_file,
        "wb"
    ) as output:

        output.write(font)


    print(
        f"RootFont: {glyph_count} glyphs"
    )

    print(
        f"RootFont: {len(font)} bytes"
    )


    return 0


if __name__ == "__main__":
    raise SystemExit(main())
