#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# AU-1 fixture generator for the Winamp `.eqf`/`winamp.q1` container.
#
# Method: written *independently* of the C++ codec from the documented Winamp
# layout (see src/audio/EqPreset.hpp) so the C++ parser/writer is checked
# against bytes it did not produce:
#   header  = b"Winamp EQ library file v1.1" + b"\x1a!--"  (31 bytes)
#   name    = 257 bytes, NUL-terminated, NUL-padded
#   values  = 10 band bytes ascending frequency + 1 preamp byte
# Slider bytes are Winamp's 6-bit field: 0 = +12 dB, 31 = 0 dB, 63 = -12 dB.
# The preset names/values are the published classic Winamp set (functional data).
#
# Usage: python3 gen_fixtures.py   (writes the .eqf/.q1 files next to it)

import os

HEADER = b"Winamp EQ library file v1.1" + b"\x1a!--"
NEUTRAL = 31

CLASSIC = [
    ("Classical", [31, 31, 31, 31, 31, 31, 44, 44, 44, 48], 31),
    ("Club", [31, 31, 26, 22, 22, 22, 26, 31, 31, 31], 31),
    ("Dance", [16, 20, 28, 32, 32, 42, 44, 44, 32, 32], 31),
    ("Laptop speakers/headphones", [24, 14, 23, 38, 36, 29, 24, 16, 11, 8], 31),
    ("Large hall", [15, 15, 22, 22, 31, 40, 40, 40, 31, 31], 31),
    ("Party", [20, 20, 31, 31, 31, 31, 31, 31, 20, 20], 31),
    ("Pop", [35, 24, 20, 19, 23, 34, 36, 36, 35, 35], 31),
    ("Reggae", [31, 31, 33, 42, 31, 21, 21, 31, 31, 31], 31),
    ("Rock", [19, 24, 41, 45, 38, 25, 17, 14, 14, 14], 31),
    ("Soft", [24, 29, 34, 36, 34, 25, 18, 16, 14, 12], 31),
    ("Ska", [36, 40, 39, 33, 25, 22, 17, 16, 14, 16], 31),
    ("Full Bass", [16, 16, 16, 22, 29, 39, 46, 49, 50, 50], 31),
    ("Soft Rock", [25, 25, 28, 33, 39, 41, 38, 33, 27, 17], 31),
    ("Full Treble", [48, 48, 48, 39, 27, 14, 6, 6, 6, 4], 31),
    ("Full Bass & Treble", [20, 22, 31, 44, 40, 29, 18, 14, 12, 12], 31),
    ("Live", [40, 31, 25, 23, 22, 22, 25, 27, 27, 28], 31),
    ("Techno", [19, 22, 31, 41, 40, 31, 19, 16, 16, 17], 31),
]


def preset(name, bands, preamp):
    raw = name.encode("utf-8")
    assert len(raw) <= 256
    name_field = raw + b"\x00" * (257 - len(raw))
    return name_field + bytes(bands + [preamp])


def library(presets):
    return HEADER + b"".join(preset(*p) for p in presets)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = {
        "winamp.q1": library(CLASSIC),
        "classical.eqf": library([CLASSIC[0]]),
        "full_treble.eqf": library([CLASSIC[13]]),
        "flat.eqf": library([("Flat", [NEUTRAL] * 10, NEUTRAL)]),
        "max.eqf": library([("Max", [0] * 10, NEUTRAL)]),
        "min.eqf": library([("Min", [63] * 10, NEUTRAL)]),
    }
    for name, data in out.items():
        with open(os.path.join(here, name), "wb") as fh:
            fh.write(data)
        print(f"{name}: {len(data)} bytes")


if __name__ == "__main__":
    main()
