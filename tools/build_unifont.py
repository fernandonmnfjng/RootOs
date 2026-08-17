#!/usr/bin/env python3

import sys
import struct


MAGIC = 0x544E4652
VERSION = 1


def convert_bitmap(hex_text):

    raw = bytes.fromhex(
        hex_text.strip()
    )


    # 8 x 16
    if len(raw) == 16:

        bitmap = bytearray(32)

        for row in range(16):

            bitmap[row * 2] = raw[row]

            bitmap[
                row * 2 + 1
            ] = 0

        return 8, bytes(bitmap)


    # 16 x 16
    if len(raw) == 32:

        return 16, raw


    return None


def main():

    if len(sys.argv) != 3:

        print(
            "usage: build_unifont.py "
            "<unifont.hex> <rootfont.bin>"
        )

        return 1


    source_path = sys.argv[1]
    output_path = sys.argv[2]


    glyphs = {}


    with open(
        source_path,
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
                line.split(
                    ":",
                    1
                )
            )


            try:

                codepoint = int(
                    code_text,
                    16
                )

            except ValueError:

                continue


            if not (
                0
                <=
                codepoint
                <=
                0x10FFFF
            ):
                continue


            converted = convert_bitmap(
                bitmap_text
            )


            if converted is None:
                continue


            width, bitmap = (
                converted
            )


            glyphs[
                codepoint
            ] = (
                width,
                bitmap
            )


    ordered = sorted(
        glyphs.items(),
        key=lambda item: item[0]
    )


    with open(
        output_path,
        "wb"
    ) as output:

        # Header
        output.write(
            struct.pack(
                "<III",
                MAGIC,
                VERSION,
                len(ordered)
            )
        )


        for (
            codepoint,
            (
                width,
                bitmap
            )
        ) in ordered:

            output.write(
                struct.pack(
                    "<IB",
                    codepoint,
                    width
                )
            )

            output.write(
                bitmap
            )


    print(
        f"RootFont glyphs: {len(ordered)}"
    )

    print(
        f"RootFont output: {output_path}"
    )


    return 0


if __name__ == "__main__":

    raise SystemExit(
        main()
    )