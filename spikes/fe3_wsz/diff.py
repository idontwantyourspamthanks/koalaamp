#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# diff.py — pixel-diff two PNGs and report the mismatch fraction. Used by the
# FE-3 spike to compare our QPainter main-window render against Webamp's.
#
# Usage: diff.py --a ours.png --b reference.png [--threshold 0.001]
#                [--mask X,Y,W,H ...]   # exclude regions from the comparison
# Exit: 0 if mismatch <= threshold, 1 otherwise, 2 on error.
import argparse
import sys

from PIL import Image


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--a", required=True)
    parser.add_argument("--b", required=True)
    parser.add_argument("--threshold", type=float, default=0.001,
                        help="max allowed mismatch fraction (default 0.1%%)")
    parser.add_argument("--mask", nargs="*", default=[],
                        help="input,region,rects X,Y,W,H to exclude")
    parser.add_argument("--diff-out", help="optional path to write a diff image")
    args = parser.parse_args()

    a = Image.open(args.a).convert("RGB")
    b = Image.open(args.b).convert("RGB")
    if a.size != b.size:
        print(f"diff: size mismatch {a.size} vs {b.size}", file=sys.stderr)
        return 2

    masks = []
    for spec in args.mask:
        try:
            boxes = tuple(int(v) for v in spec.split(","))
        except ValueError:
            print(f"diff: bad --mask spec '{spec}'", file=sys.stderr)
            return 2
        masks.append(boxes)

    def masked(x: int, y: int) -> bool:
        return any(x0 <= x < x0 + w and y0 <= y < y0 + h for x0, y0, w, h in masks)

    ap, bp = a.load(), b.load()
    total = 0
    mismatch = 0
    diff = Image.new("RGB", a.size, (0, 0, 0))
    dp = diff.load()
    for y in range(a.height):
        for x in range(a.width):
            if masked(x, y):
                continue
            total += 1
            if ap[x, y] != bp[x, y]:
                mismatch += 1
                dp[x, y] = (255, 0, 0)

    fraction = mismatch / total if total else 0.0
    print(f"diff: {mismatch}/{total} compared pixels differ = {fraction * 100:.4f}% "
          f"(threshold {args.threshold * 100:.4f}%)")
    if args.diff_out:
        diff.save(args.diff_out)
    return 0 if fraction <= args.threshold else 1


if __name__ == "__main__":
    sys.exit(main())
