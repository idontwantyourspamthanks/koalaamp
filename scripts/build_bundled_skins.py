#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build KoalaAmp's bundled classic skins.

Each skin is a Winamp 2 `.wsz` (a zip of BMP sheets + text). Sprite rectangles
match `src/skins/SkinSprites.hpp`. The plates and koala faces in `skins/art/`
are original artwork; glyphs, bevels and the KOALAAMP wordmark are drawn on
the pixel grid so they stay sharp at 1×.

No Winamp skin artwork is used.
"""

from __future__ import annotations

import io
import zipfile
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "skins" / "art"
OUT = ROOT / "skins"
PREVIEW = Path("/tmp/koala-skin-preview")

# Reference minima from src/skins/SkinSet.cpp. Sheets may be larger.
MIN_SIZE = {
    "main.bmp": (275, 116),
    "titlebar.bmp": (344, 87),
    "cbuttons.bmp": (136, 36),
    "posbar.bmp": (307, 10),
    "volume.bmp": (68, 433),
    "balance.bmp": (68, 433),
    "shufrep.bmp": (92, 85),
    "monoster.bmp": (58, 24),
    "playpaus.bmp": (42, 9),
    "numbers.bmp": (99, 13),
    "nums_ex.bmp": (99, 13),
    "text.bmp": (155, 74),
    "pledit.bmp": (280, 186),
    "eqmain.bmp": (275, 315),
    "eq_ex.bmp": (275, 82),
    "gen.bmp": (194, 109),
}

Rgb = tuple[int, int, int]


def rgb(r: int, g: int, b: int) -> Rgb:
    return (max(0, min(255, r)), max(0, min(255, g)), max(0, min(255, b)))


def mix(a: Rgb, b: Rgb, t: float) -> Rgb:
    return rgb(
        int(a[0] + (b[0] - a[0]) * t),
        int(a[1] + (b[1] - a[1]) * t),
        int(a[2] + (b[2] - a[2]) * t),
    )


def shade(c: Rgb, t: float) -> Rgb:
    if t >= 0:
        return mix(c, (255, 255, 255), t)
    return mix(c, (0, 0, 0), -t)


def hex_rgb(c: Rgb) -> str:
    return f"#{c[0]:02X}{c[1]:02X}{c[2]:02X}"


# --- 5×6 bitmap font (text.bmp). One cell is 5×6, right column usually blank. ---

def _g(rows: list[str]) -> list[str]:
    if len(rows) != 6 or any(len(r) != 5 for r in rows):
        raise ValueError(f"glyph must be 6×5, got {rows!r}")
    return rows


# Each glyph is six rows of five characters. '#' is ink.
_RAW: dict[str, list[str]] = {
    "a": [" ## #", "#  ##", "#####", "#  # ", "#  # ", "     "],
    "b": ["###  ", "#  # ", "###  ", "#  # ", "###  ", "     "],
    "c": [" ### ", "#    ", "#    ", "#    ", " ### ", "     "],
    "d": ["###  ", "#  # ", "#  # ", "#  # ", "###  ", "     "],
    "e": ["#### ", "#    ", "###  ", "#    ", "#### ", "     "],
    "f": ["#### ", "#    ", "###  ", "#    ", "#    ", "     "],
    "g": [" ### ", "#  # ", "#  # ", " ### ", "   # ", " ##  "],
    "h": ["#  # ", "#  # ", "#### ", "#  # ", "#  # ", "     "],
    "i": [" ### ", "  #  ", "  #  ", "  #  ", " ### ", "     "],
    "j": ["  ## ", "   # ", "   # ", "#  # ", " ##  ", "     "],
    "k": ["#  # ", "# #  ", "##   ", "# #  ", "#  # ", "     "],
    "l": ["#    ", "#    ", "#    ", "#    ", "#### ", "     "],
    "m": ["# # #", "## ##", "# # #", "# # #", "# # #", "     "],
    "n": ["#  # ", "## # ", "# # #", "#  # ", "#  # ", "     "],
    "o": [" ### ", "#   #", "#   #", "#   #", " ### ", "     "],
    "p": ["###  ", "#  # ", "###  ", "#    ", "#    ", "     "],
    "q": [" ### ", "#   #", "#   #", " ### ", "   # ", "     "],
    "r": ["###  ", "#  # ", "###  ", "# #  ", "#  # ", "     "],
    "s": [" ### ", "#    ", " ### ", "    #", " ### ", "     "],
    "t": ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "     "],
    "u": ["#  # ", "#  # ", "#  # ", "#  # ", " ### ", "     "],
    "v": ["#   #", "#   #", "#   #", " # # ", "  #  ", "     "],
    "w": ["#   #", "#   #", "# # #", "# # #", " # # ", "     "],
    "x": ["#   #", " # # ", "  #  ", " # # ", "#   #", "     "],
    "y": ["#   #", "#   #", " # # ", "  #  ", " #   ", "#    "],
    "z": ["#####", "   # ", "  #  ", " #   ", "#####", "     "],
    '"': ["# #  ", "# #  ", "     ", "     ", "     ", "     "],
    "@": [" ### ", "#   #", "# ## ", "# #  ", " ### ", "     "],
    " ": ["     ", "     ", "     ", "     ", "     ", "     "],
    "0": [" ### ", "#   #", "# # #", "#   #", " ### ", "     "],
    "1": [" ##  ", "  #  ", "  #  ", "  #  ", " ### ", "     "],
    "2": [" ### ", "#   #", "  ## ", " #   ", "#####", "     "],
    "3": ["#### ", "    #", " ### ", "    #", "#### ", "     "],
    "4": ["#  # ", "#  # ", "#####", "   # ", "   # ", "     "],
    "5": ["#####", "#    ", "#### ", "    #", "#### ", "     "],
    "6": [" ### ", "#    ", "#### ", "#   #", " ### ", "     "],
    "7": ["#####", "    #", "   # ", "  #  ", "  #  ", "     "],
    "8": [" ### ", "#   #", " ### ", "#   #", " ### ", "     "],
    "9": [" ### ", "#   #", " ####", "    #", " ### ", "     "],
    "…": ["     ", "     ", "     ", "     ", "# # #", "     "],
    ".": ["     ", "     ", "     ", "     ", " ##  ", "     "],
    ":": ["     ", " ##  ", "     ", " ##  ", "     ", "     "],
    "(": ["  #  ", " #   ", " #   ", " #   ", "  #  ", "     "],
    ")": [" #   ", "  #  ", "  #  ", "  #  ", " #   ", "     "],
    "-": ["     ", "     ", " ### ", "     ", "     ", "     "],
    "'": [" #   ", " #   ", "     ", "     ", "     ", "     "],
    "!": [" #   ", " #   ", " #   ", "     ", " #   ", "     "],
    "_": ["     ", "     ", "     ", "     ", "#####", "     "],
    "+": ["     ", "  #  ", " ### ", "  #  ", "     ", "     "],
    "\\": ["#    ", "#    ", " #   ", "  #  ", "   # ", "   # "],
    "/": ["   # ", "   # ", "  #  ", " #   ", "#    ", "#    "],
    "[": [" ### ", " #   ", " #   ", " #   ", " ### ", "     "],
    "]": [" ### ", "   # ", "   # ", "   # ", " ### ", "     "],
    "^": [" # # ", "#   #", "     ", "     ", "     ", "     "],
    "&": [" ##  ", "# #  ", " #   ", "# #  ", " ## #", "     "],
    "%": ["#   #", "   # ", "  #  ", " #   ", "#   #", "     "],
    ",": ["     ", "     ", "     ", " #   ", " #   ", "#    "],
    "=": ["     ", " ### ", "     ", " ### ", "     ", "     "],
    "$": [" ### ", "# #  ", " ### ", "  # #", " ### ", "     "],
    "#": [" # # ", "#####", " # # ", "#####", " # # ", "     "],
    "Å": [" # # ", " ### ", "#   #", "#####", "#   #", "#   #"],
    "Ö": ["#   #", " ### ", "#   #", "#   #", " ### ", "     "],
    "Ä": ["#   #", " ### ", "#   #", "#####", "#   #", "#   #"],
    "?": [" ### ", "#   #", "  ## ", "     ", "  #  ", "     "],
    "*": [" # # ", " ### ", "#####", " ### ", " # # ", "     "],
}

FONT = {k: _g(v) for k, v in _RAW.items()}

# text.bmp cell origins. Mirrors fontGlyph() in SkinSprites.hpp.
PLACEMENT = [
    ("a", 0, 0), ("b", 5, 0), ("c", 10, 0), ("d", 15, 0), ("e", 20, 0),
    ("f", 25, 0), ("g", 30, 0), ("h", 35, 0), ("i", 40, 0), ("j", 45, 0),
    ("k", 50, 0), ("l", 55, 0), ("m", 60, 0), ("n", 65, 0), ("o", 70, 0),
    ("p", 75, 0), ("q", 80, 0), ("r", 85, 0), ("s", 90, 0), ("t", 95, 0),
    ("u", 100, 0), ("v", 105, 0), ("w", 110, 0), ("x", 115, 0), ("y", 120, 0),
    ("z", 125, 0), ('"', 130, 0), ("@", 135, 0), (" ", 150, 0),
    ("0", 0, 6), ("1", 5, 6), ("2", 10, 6), ("3", 15, 6), ("4", 20, 6),
    ("5", 25, 6), ("6", 30, 6), ("7", 35, 6), ("8", 40, 6), ("9", 45, 6),
    ("…", 50, 6), (".", 55, 6), (":", 60, 6), ("(", 65, 6), (")", 70, 6),
    ("-", 75, 6), ("'", 80, 6), ("!", 85, 6), ("_", 90, 6), ("+", 95, 6),
    ("\\", 100, 6), ("/", 105, 6), ("[", 110, 6), ("]", 115, 6), ("^", 120, 6),
    ("&", 125, 6), ("%", 130, 6), (",", 135, 6), ("=", 140, 6), ("$", 145, 6),
    ("#", 150, 6),
    ("Å", 0, 12), ("Ö", 5, 12), ("Ä", 10, 12), ("?", 15, 12), ("*", 20, 12),
]


def new(w: int, h: int, color: Rgb) -> Image.Image:
    return Image.new("RGB", (w, h), color)


def fill(im: Image.Image, x: int, y: int, w: int, h: int, color: Rgb) -> None:
    if w <= 0 or h <= 0:
        return
    im.paste(color, (x, y, x + w, y + h))


def pixel(im: Image.Image, x: int, y: int, color: Rgb) -> None:
    w, h = im.size
    if 0 <= x < w and 0 <= y < h:
        im.putpixel((x, y), color)


def hline(im: Image.Image, x: int, y: int, w: int, color: Rgb) -> None:
    fill(im, x, y, w, 1, color)


def vline(im: Image.Image, x: int, y: int, h: int, color: Rgb) -> None:
    fill(im, x, y, 1, h, color)


def bevel(im: Image.Image, x: int, y: int, w: int, h: int, hi: Rgb, lo: Rgb) -> None:
    hline(im, x, y, w, hi)
    vline(im, x, y, h, hi)
    hline(im, x, y + h - 1, w, lo)
    vline(im, x + w - 1, y, h, lo)


def inset(im: Image.Image, x: int, y: int, w: int, h: int, color: Rgb, hi: Rgb, lo: Rgb) -> None:
    fill(im, x, y, w, h, color)
    # Recessed: dark on the top/left, light on the bottom/right.
    bevel(im, x, y, w, h, lo, hi)


def blit_glyph(im: Image.Image, glyph: list[str], x: int, y: int, fg: Rgb, bg: Rgb | None) -> None:
    for row, line in enumerate(glyph):
        for col, ch in enumerate(line):
            if ch != " " and ch != ".":
                pixel(im, x + col, y + row, fg)
            elif bg is not None:
                pixel(im, x + col, y + row, bg)


def text_width(s: str) -> int:
    return len(s) * 5


def draw_text(
    im: Image.Image,
    s: str,
    x: int,
    y: int,
    fg: Rgb,
    bg: Rgb | None = None,
    shadow: Rgb | None = None,
) -> None:
    pen = x
    for ch in s:
        glyph = FONT.get(ch.lower(), FONT["?"])
        if shadow is not None:
            blit_glyph(im, glyph, pen + 1, y + 1, shadow, None)
        blit_glyph(im, glyph, pen, y, fg, bg)
        pen += 5


def centered(im: Image.Image, s: str, y: int, fg: Rgb, bg: Rgb | None = None) -> None:
    draw_text(im, s, (im.width - text_width(s)) // 2, y, fg, bg)


# 11×11 title-bar koala. Letters select a palette key.
KOALA_MASK = [
    " ee   ee  ",
    "eie   eie ",
    "eefffffefe",
    "ff yf yf f",
    "ff p  p ff",
    "fff nnn ff",
    " ff nnn f ",
    "  fffff   ",
    "   fff    ",
    "    n     ",
    "          ",
]


def draw_koala(im: Image.Image, x: int, y: int, palette: dict[str, Rgb]) -> None:
    for row, line in enumerate(KOALA_MASK):
        for col, ch in enumerate(line):
            if ch in palette:
                pixel(im, x + col, y + row, palette[ch])


def koala_colors(fur: Rgb, ear: Rgb, inner: Rgb, eye: Rgb, nose: Rgb) -> dict[str, Rgb]:
    return {"f": fur, "e": ear, "i": inner, "y": eye, "p": (20, 20, 24), "n": nose}


# --- plates and faces -------------------------------------------------------

def load_plate(name: str, size: tuple[int, int]) -> Image.Image:
    im = Image.open(ART / f"koala-{name}-plate.jpg").convert("RGB")
    im = im.resize(size, Image.Resampling.BOX)
    return ImageOps.posterize(im, 3)


def content_box(im: Image.Image, tol: int = 36) -> tuple[int, int, int, int]:
    bg = im.getpixel((1, 1))
    w, h = im.size
    px = im.load()
    minx, miny, maxx, maxy = w, h, 0, 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            p = px[x, y]
            if abs(p[0] - bg[0]) + abs(p[1] - bg[1]) + abs(p[2] - bg[2]) > tol:
                minx = min(minx, x)
                miny = min(miny, y)
                maxx = max(maxx, x)
                maxy = max(maxy, y)
    pad = 6
    return (max(0, minx - pad), max(0, miny - pad), min(w, maxx + pad), min(h, maxy + pad))


def load_face(name: str, size: tuple[int, int], tight: bool) -> Image.Image:
    im = Image.open(ART / f"koala-{name}-face.jpg").convert("RGB")
    if tight:
        im = im.crop(content_box(im))
    return im.resize(size, Image.Resampling.BOX)


# --- buttons ----------------------------------------------------------------

def gel(w: int, h: int, top: Rgb, bot: Rgb, ink_border: bool = True) -> Image.Image:
    im = new(w, h, top)
    for y in range(h):
        t = y / max(1, h - 1)
        fill(im, 0, y, w, 1, mix(top, bot, t))
    if ink_border and w > 2 and h > 2:
        bevel(im, 0, 0, w, h, shade(top, 0.45), shade(bot, -0.45))
    return im


def pressed(im: Image.Image) -> Image.Image:
    # Invert the vertical gradient and darken, the way a gel button sinks.
    w, h = im.size
    out = im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    px = out.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = shade(px[x, y], -0.18)
    return out


def stamp(im: Image.Image, mask: list[str], fg: Rgb, dx: int = 0, dy: int = 0) -> None:
    mh = len(mask)
    mw = max(len(r) for r in mask)
    x0 = (im.width - mw) // 2 + dx
    y0 = (im.height - mh) // 2 + dy
    blit_glyph(im, mask, x0, y0, fg, None)


PLAY = ["  #  ", "  ## ", "  ###", "  ## ", "  #  "]
PAUSE = ["# #  ", "# #  ", "# #  ", "# #  ", "# #  "]
STOP = ["#### ", "#  # ", "#  # ", "#### ", "     "]
PREV = ["# #  ", "# ## ", "# ###", "# ## ", "# #  "]
NEXT = ["  # #", " ## #", "### #", " ## #", "  # #"]
EJECT = ["  #  ", " ### ", "#####", "     ", "#####"]
MENU = ["###  ", "     ", "###  ", "     ", "###  "]
MINI = ["     ", "     ", "     ", "#### ", "     "]
SHADE = ["#### ", "#  # ", "#  # ", "#### ", "     "]
CLOSE = ["#   #", " # # ", "  #  ", " # # ", "#   #"]
REPEAT = [" ## #", "#  # ", "     ", " #  #", "# ## "]
SHUF = ["#   #", " # # ", "  #  ", " # # ", "#   #"]


def icon_button(w: int, h: int, top: Rgb, bot: Rgb, mask: list[str], fg: Rgb, down: bool = False) -> Image.Image:
    face = gel(w, h, bot if down else top, top if down else bot)
    stamp(face, mask, fg, 1 if down else 0, 1 if down else 0)
    return face


def label_button(w: int, h: int, top: Rgb, bot: Rgb, label: str, fg: Rgb, down: bool = False) -> Image.Image:
    face = gel(w, h, bot if down else top, top if down else bot)
    tw = text_width(label)
    draw_text(face, label, (w - tw) // 2 + (1 if down else 0), (h - 6) // 2 + (1 if down else 0), fg)
    return face


# --- digits (9×13, 7-segment). Digit 2's midline is the minus-sign slice. ---

def draw_digit(im: Image.Image, x: int, y: int, n: int, fg: Rgb, bg: Rgb) -> None:
    fill(im, x, y, 9, 13, bg)
    segs = {
        0: "abcedf",
        1: "bc",
        2: "abged",
        3: "abgcd",
        4: "fgbc",
        5: "afgcd",
        6: "afgedc",
        7: "abc",
        8: "abcdefg",
        9: "abfgcd",
    }[n]
    # Local coordinates inside the 9×13 cell. `g` occupies y=6, x=2..6, which
    # is the 5×1 slice Winamp borrows from digit 2 as the minus sign.
    boxes = {
        "a": (2, 1, 5, 2),
        "b": (6, 2, 2, 4),
        "c": (6, 7, 2, 4),
        "d": (2, 10, 5, 2),
        "e": (1, 7, 2, 4),
        "f": (1, 2, 2, 4),
        "g": (2, 6, 5, 2),
    }
    for name in segs:
        sx, sy, sw, sh = boxes[name]
        fill(im, x + sx, y + sy, sw, sh, fg)


def draw_minus_cell(im: Image.Image, x: int, y: int, fg: Rgb, bg: Rgb) -> None:
    fill(im, x, y, 9, 13, bg)
    fill(im, x + 2, y + 6, 5, 2, fg)


# --- themed sheets ----------------------------------------------------------

@dataclass(frozen=True)
class Theme:
    key: str
    filename: str
    title: str
    egg: str
    panel: Rgb
    panel_hi: Rgb
    panel_lo: Rgb
    lcd: Rgb
    ink: Rgb
    dim: Rgb
    accent: Rgb
    accent2: Rgb
    hot: Rgb
    groove: Rgb
    fill: Rgb
    title_top: Rgb
    title_bot: Rgb
    title_ink: Rgb
    button_top: Rgb
    button_bot: Rgb
    button_ink: Rgb
    on_top: Rgb
    on_bot: Rgb
    on_ink: Rgb
    fur: Rgb
    ear: Rgb
    inner: Rgb
    nose: Rgb
    eye: Rgb
    spectrum_hot: Rgb
    spectrum_mid: Rgb
    spectrum_dim: Rgb
    pledit_bg: Rgb
    pledit_sel: Rgb
    pledit_text: Rgb
    pledit_cur: Rgb


def lerp_n(stops: list[Rgb], n: int) -> list[Rgb]:
    if n <= 1:
        return [stops[0]]
    out = []
    for i in range(n):
        t = i / (n - 1)
        scaled = t * (len(stops) - 1)
        idx = min(int(scaled), len(stops) - 2)
        local = scaled - idx
        out.append(mix(stops[idx], stops[idx + 1], local))
    return out


def sanitize(im: Image.Image) -> Image.Image:
    # gen.bmp treats RGB(0, 198, 255) as a tile marker. Keep it out of the art.
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            if px[x, y] == (0, 198, 255):
                px[x, y] = (0, 197, 255)
    return im


def build_text(theme: Theme) -> Image.Image:
    im = new(155, 74, theme.lcd)
    for ch, x, y in PLACEMENT:
        blit_glyph(im, FONT[ch], x, y, theme.ink, theme.lcd)
    return im


def build_numbers(theme: Theme) -> tuple[Image.Image, Image.Image]:
    numbers = new(99, 13, theme.lcd)
    for n in range(10):
        draw_digit(numbers, n * 9, 0, n, theme.hot, theme.lcd)
    ex = new(108, 13, theme.lcd)
    for n in range(10):
        draw_digit(ex, n * 9, 0, n, theme.hot, theme.lcd)
    draw_digit(ex, 90, 0, 8, theme.lcd, theme.lcd)  # empty no-minus cell
    fill(ex, 90, 0, 9, 13, theme.lcd)
    draw_minus_cell(ex, 99, 0, theme.hot, theme.lcd)
    return numbers, ex


def build_main(theme: Theme) -> Image.Image:
    im = load_plate(theme.key, (275, 116))
    # Wells only where live text has to sit. The rest of the plate stays visible.
    inset(im, 34, 24, 70, 16, theme.lcd, theme.panel_hi, theme.panel_lo)
    inset(im, 108, 26, 158, 8, theme.lcd, theme.panel_hi, theme.panel_lo)
    inset(im, 22, 41, 80, 20, theme.lcd, theme.panel_hi, theme.panel_lo)
    # Colon between the minute pair (x=60..69) and the second pair (x=78).
    pixel(im, 72, 29, theme.hot)
    pixel(im, 73, 29, theme.hot)
    pixel(im, 72, 34, theme.hot)
    pixel(im, 73, 34, theme.hot)
    # Signature along the bottom lip, which the transport buttons do not cover.
    label = "KOALAAMP"
    draw_text(im, label, (275 - text_width(label)) // 2, 109, theme.hot, None, (0, 0, 0))
    return im


def title_strip(theme: Theme, label: str, selected: bool, egg: bool) -> Image.Image:
    top = theme.title_top if selected else shade(theme.title_top, -0.25)
    bot = theme.title_bot if selected else shade(theme.title_bot, -0.35)
    im = gel(275, 14, top, bot)
    ink = theme.title_ink if selected else shade(theme.title_ink, -0.25)
    if egg:
        draw_text(im, label, 18, 4, theme.accent2 if selected else theme.dim)
    else:
        draw_koala(
            im,
            16,
            2,
            koala_colors(theme.fur, theme.ear, theme.inner, theme.eye, theme.nose),
        )
        draw_text(im, label, 30, 4, ink)
    # Idle window buttons live in the bar. Held states are separate sprites.
    for bx, mask in ((6, MENU), (244, MINI), (254, SHADE), (264, CLOSE)):
        btn = icon_button(9, 9, theme.button_top, theme.button_bot, mask, theme.button_ink)
        im.paste(btn, (bx, 3))
    if selected:
        hline(im, 0, 13, 275, theme.accent)
    return im


def shade_strip(theme: Theme, selected: bool) -> Image.Image:
    im = title_strip(theme, "KOALAAMP", selected, False)
    # Transport glyphs are baked into the shade bar (there is no cbuttons pass).
    glyphs = [(169, PREV), (176, PLAY), (186, PAUSE), (195, STOP), (204, NEXT), (215, EJECT)]
    widths = [7, 10, 9, 9, 10, 10]
    for (x, mask), w in zip(glyphs, widths):
        face = icon_button(w, 10, theme.button_top, theme.button_bot, mask, theme.accent)
        im.paste(face, (x, 2))
    return im


def build_titlebar(theme: Theme) -> Image.Image:
    im = new(344, 87, theme.panel_lo)
    im.paste(title_strip(theme, "KOALAAMP", True, False), (27, 0))
    im.paste(title_strip(theme, "KOALAAMP", False, False), (27, 15))
    im.paste(shade_strip(theme, True), (27, 29))
    im.paste(shade_strip(theme, False), (27, 42))
    im.paste(title_strip(theme, theme.egg, True, True), (27, 57))
    im.paste(title_strip(theme, theme.egg, False, True), (27, 72))

    def winbtn(mask: list[str], down: bool) -> Image.Image:
        return icon_button(9, 9, theme.button_top, theme.button_bot, mask, theme.button_ink, down)

    im.paste(winbtn(MENU, False), (0, 0))
    im.paste(winbtn(MENU, True), (0, 9))
    im.paste(winbtn(MINI, False), (9, 0))
    im.paste(winbtn(MINI, True), (9, 9))
    im.paste(winbtn(CLOSE, False), (18, 0))
    im.paste(winbtn(CLOSE, True), (18, 9))
    im.paste(winbtn(SHADE, False), (0, 18))
    im.paste(winbtn(SHADE, True), (9, 18))
    im.paste(winbtn(SHADE, False), (0, 27))
    im.paste(winbtn(SHADE, True), (9, 27))
    # Shade seek groove + 3×7 thumbs.
    groove = new(17, 7, theme.groove)
    inset(groove, 0, 0, 17, 7, theme.groove, theme.panel_hi, theme.panel_lo)
    im.paste(groove, (0, 36))
    for dx, col in ((17, theme.dim), (20, theme.fill), (23, theme.hot)):
        thumb = new(3, 7, col)
        bevel(thumb, 0, 0, 3, 7, shade(col, 0.4), shade(col, -0.4))
        im.paste(thumb, (dx, 36))

    def clutter(colors_ink: Rgb, back: Rgb) -> Image.Image:
        bar = new(8, 43, back)
        bevel(bar, 0, 0, 8, 43, shade(back, 0.3), shade(back, -0.4))
        letters = [("O", 3), ("A", 11), ("I", 18), ("D", 25), ("V", 33)]
        for ch, yy in letters:
            draw_text(bar, ch, 1, yy, colors_ink)
        return bar

    im.paste(clutter(theme.ink, theme.lcd), (304, 0))
    im.paste(clutter(theme.dim, shade(theme.lcd, -0.2)), (312, 0))

    def lit_letter(ch: str, w: int, h: int) -> Image.Image:
        cell = new(w, h, theme.accent)
        draw_text(cell, ch, 1, max(0, (h - 6) // 2), theme.on_ink)
        return cell

    im.paste(lit_letter("O", 8, 8), (304, 47))
    im.paste(lit_letter("A", 8, 7), (312, 55))
    im.paste(lit_letter("I", 8, 7), (320, 62))
    im.paste(lit_letter("D", 8, 8), (328, 69))
    im.paste(lit_letter("V", 8, 7), (336, 77))
    return im


def build_cbuttons(theme: Theme) -> Image.Image:
    im = new(136, 36, theme.panel_lo)
    spec = [
        (0, 23, 18, PREV),
        (23, 23, 18, PLAY),
        (46, 23, 18, PAUSE),
        (69, 23, 18, STOP),
        (92, 23, 18, NEXT),
        (114, 22, 16, EJECT),
    ]
    for x, w, h, mask in spec:
        lit = mask is PLAY
        fg = theme.on_ink if lit else theme.button_ink
        top = theme.on_top if lit else theme.button_top
        bot = theme.on_bot if lit else theme.button_bot
        im.paste(icon_button(w, h, top, bot, mask, fg), (x, 0))
        down_h = 16 if mask is EJECT else 18
        down_w = 22 if mask is NEXT else w
        im.paste(icon_button(down_w, down_h, top, bot, mask, fg, True), (x, 16 if mask is EJECT else 18))
    return im


def build_posbar(theme: Theme) -> Image.Image:
    im = new(307, 10, theme.groove)
    inset(im, 0, 0, 248, 10, theme.groove, theme.panel_hi, theme.panel_lo)
    for x in range(8, 240, 16):
        vline(im, x, 2, 6, shade(theme.groove, 0.25))
    fill(im, 2, 4, 244, 2, shade(theme.fill, -0.55))

    def thumb(hot: bool) -> Image.Image:
        col = theme.hot if hot else theme.button_top
        face = gel(29, 10, shade(col, 0.2), shade(col, -0.35))
        for x in (12, 14, 16):
            vline(face, x, 2, 6, shade(theme.button_ink, -0.1))
        return face

    im.paste(thumb(False), (248, 0))
    im.paste(thumb(True), (278, 0))
    return im


def h_slider_frames(theme: Theme, width: int, origin_x: int, grow: str) -> Image.Image:
    im = new(68, 433, theme.panel_lo)
    track_w = 38 if grow == "center" else 64
    track_x = origin_x
    for i in range(28):
        y = i * 15
        fill(im, 0, y, 68, 13, theme.panel)
        inset(im, track_x, y + 3, track_w, 7, theme.groove, theme.panel_hi, theme.panel_lo)
        t = i / 27
        if grow == "left":
            fw = max(0, round(t * (track_w - 4)))
            if fw:
                fill(im, track_x + 2, y + 4, fw, 5, theme.fill)
        else:
            half = round(t * ((track_w - 4) / 2))
            cx = track_x + track_w // 2
            fill(im, cx - max(half, 1), y + 4, max(half, 1) * 2, 5, theme.fill)
    return im


def slider_thumb(theme: Theme, hot: bool) -> Image.Image:
    col = theme.hot if hot else theme.button_top
    face = gel(14, 11, shade(col, 0.25), shade(col, -0.4))
    # One ridge. Two bars read as the pause glyph.
    vline(face, 7, 2, 7, shade(theme.button_ink, -0.15))
    return face


def build_volume(theme: Theme) -> Image.Image:
    im = h_slider_frames(theme, 68, 2, "left")
    im.paste(slider_thumb(theme, True), (0, 422))
    im.paste(slider_thumb(theme, False), (15, 422))
    return im


def build_balance(theme: Theme) -> Image.Image:
    # The displayed groove starts at x=9. The first nine columns stay spare.
    im = h_slider_frames(theme, 68, 9, "center")
    im.paste(slider_thumb(theme, True), (0, 422))
    im.paste(slider_thumb(theme, False), (15, 422))
    return im


def build_shufrep(theme: Theme) -> Image.Image:
    im = new(92, 85, theme.panel_lo)
    states = [(False, False), (False, True), (True, False), (True, True)]
    for i, (on, down) in enumerate(states):
        y = i * 15
        rep = label_button(
            28, 15,
            theme.on_top if on else theme.button_top,
            theme.on_bot if on else theme.button_bot,
            "REP",
            theme.on_ink if on else theme.button_ink,
            down,
        )
        shuf = label_button(
            47, 15,
            theme.on_top if on else theme.button_top,
            theme.on_bot if on else theme.button_bot,
            "SHUFFLE",
            theme.on_ink if on else theme.button_ink,
            down,
        )
        im.paste(rep, (0, y))
        im.paste(shuf, (28, y))
    # EQ / PL. Selected is the lower row of each pair; depressed is x>=46.
    slots = {
        (False, False): (0, 61),
        (True, False): (0, 73),
        (False, True): (46, 61),
        (True, True): (46, 73),
    }
    for (on, down), (x, y) in slots.items():
        im.paste(
            label_button(
                23, 12,
                theme.on_top if on else theme.button_top,
                theme.on_bot if on else theme.button_bot,
                "EQ",
                theme.on_ink if on else theme.button_ink,
                down,
            ),
            (x, y),
        )
        im.paste(
            label_button(
                23, 12,
                theme.on_top if on else theme.button_top,
                theme.on_bot if on else theme.button_bot,
                "PL",
                theme.on_ink if on else theme.button_ink,
                down,
            ),
            (x + 23, y),
        )
    return im


# 4×5 letters. The 5×6 font's "O" loses its right stem if cropped, and six
# full cells do not fit in the 29px stereo sprite.
TINY = {
    "S": ["###.", "#...", "##..", "..#.", "###."],
    "T": ["####", ".#..", ".#..", ".#..", ".#.."],
    "E": ["###.", "#...", "##..", "#...", "###."],
    "R": ["##.#", "#..#", "##..", "#.#.", "#..#"],
    "O": [".##.", "#..#", "#..#", "#..#", ".##."],
    "M": ["#..#", "####", "#..#", "#..#", "#..#"],
    "N": ["#..#", "##.#", "#.##", "#..#", "#..#"],
}


def build_monoster(theme: Theme) -> Image.Image:
    im = new(58, 24, theme.lcd)

    def word(text: str, w: int, h: int, fg: Rgb, bg: Rgb) -> Image.Image:
        cell = new(w, h, bg)
        pen = (w - len(text) * 4) // 2
        y = (h - 5) // 2
        for ch in text:
            blit_glyph(cell, TINY[ch], pen, y, fg, None)
            pen += 4
        return cell

    lit = mix(theme.lcd, theme.accent, 0.45)
    im.paste(word("STEREO", 29, 12, theme.on_ink, lit), (0, 0))
    im.paste(word("STEREO", 29, 12, theme.dim, theme.lcd), (0, 12))
    im.paste(word("MONO", 27, 12, theme.on_ink, lit), (29, 0))
    im.paste(word("MONO", 27, 12, theme.dim, theme.lcd), (29, 12))
    return im


def build_playpaus(theme: Theme) -> Image.Image:
    im = new(42, 9, theme.lcd)
    marks = [(0, PLAY, theme.fill), (9, PAUSE, theme.accent2), (18, STOP, theme.dim)]
    for x, mask, col in marks:
        cell = new(9, 9, theme.lcd)
        stamp(cell, mask, col)
        im.paste(cell, (x, 0))
    # Work-indicator slivers: idle at x=36, lit at x=39. Each blit is 3px wide.
    fill(im, 36, 2, 3, 5, theme.dim)
    fill(im, 39, 2, 3, 5, theme.hot)
    return im


def build_pledit(theme: Theme) -> Image.Image:
    im = new(280, 186, theme.panel_lo)

    def bar(w: int, h: int, selected: bool) -> Image.Image:
        # Vertical gradient only. A side bevel would seam every time the 25px
        # tile repeats.
        top = theme.title_top if selected else shade(theme.title_top, -0.28)
        bot = theme.title_bot if selected else shade(theme.title_bot, -0.4)
        face = new(w, h, top)
        for yy in range(h):
            fill(face, 0, yy, w, 1, mix(top, bot, yy / max(1, h - 1)))
        return face

    def title_piece(w: int, h: int, selected: bool, kind: str) -> Image.Image:
        face = bar(w, h, selected)
        if kind == "title":
            face_im = load_face(theme.key, (16, 16), tight=True)
            face.paste(face_im, (4, 2))
            draw_text(face, "KOALAAMP", 22, 7, theme.title_ink)
        if kind == "right":
            face.paste(icon_button(9, 9, theme.button_top, theme.button_bot, SHADE, theme.button_ink), (4, 5))
            face.paste(icon_button(9, 9, theme.button_top, theme.button_bot, CLOSE, theme.button_ink), (14, 5))
        return face

    for selected, y in ((True, 0), (False, 21)):
        im.paste(title_piece(25, 20, selected, "left"), (0, y))
        im.paste(title_piece(100, 20, selected, "title"), (26, y))
        im.paste(title_piece(25, 20, selected, "tile"), (127, y))
        im.paste(title_piece(25, 20, selected, "right"), (153, y))

    def side(w: int, h: int, outer_on_left: bool) -> Image.Image:
        # Horizontal gradient only, so the 29px tile repeats without a seam.
        outer = theme.panel_hi
        inner = theme.panel_lo
        left, right = (outer, inner) if outer_on_left else (inner, outer)
        face = new(w, h, left)
        for xx in range(w):
            vline(face, xx, 0, h, mix(left, right, xx / max(1, w - 1)))
        return face

    im.paste(side(12, 29, True), (0, 42))
    right = side(20, 29, False)
    # Full-height track so the 29px tile repeats as one scrollbar.
    fill(right, 6, 0, 6, 29, theme.groove)
    im.paste(right, (31, 42))

    def bottom_bg(w: int, h: int) -> Image.Image:
        return bar(w, h, False)

    bl = bottom_bg(125, 38)
    labels = ["ADD", "REM", "SEL", "MISC"]
    for i, label in enumerate(labels):
        bl.paste(
            label_button(22, 18, theme.button_top, theme.button_bot, label, theme.button_ink),
            (14 + i * 29, 8),
        )
    im.paste(bl, (0, 72))

    br = bottom_bg(150, 38)
    # Time readout pocket. Glyphs are opaque and use the LCD colour.
    inset(br, 4, 8, 94, 12, theme.lcd, theme.panel_hi, theme.panel_lo)
    icons = [PREV, PLAY, PAUSE, STOP, NEXT, EJECT]
    for i, mask in enumerate(icons):
        fg = theme.accent if mask is PLAY else theme.button_ink
        br.paste(icon_button(10, 10, theme.button_top, theme.button_bot, mask, fg), (3 + i * 10, 22))
    inset(br, 64, 22, 28, 8, theme.lcd, theme.panel_hi, theme.panel_lo)
    face = load_face(theme.key, (28, 28), tight=True)
    br.paste(face, (100, 4))
    # Resize grip.
    for i in range(4):
        hline(br, 132 + i * 3, 30 - i * 3, 16 - i * 3, theme.dim)
    im.paste(br, (126, 72))

    tile = bottom_bg(25, 38)
    im.paste(tile, (179, 0))

    vis = load_face(theme.key, (75, 38), tight=False)
    im.paste(vis, (205, 0))

    # Shade bars.
    shade_l = bar(25, 14, True)
    draw_text(shade_l, "KA", 2, 4, theme.title_ink)
    im.paste(shade_l, (72, 42))
    shade_m = bar(25, 14, False)
    im.paste(shade_m, (72, 57))
    for selected, y in ((True, 42), (False, 57)):
        piece = bar(50, 14, selected)
        piece.paste(icon_button(9, 9, theme.button_top, theme.button_bot, CLOSE, theme.button_ink), (39, 3))
        im.paste(piece, (99, y))

    handle = gel(8, 18, theme.button_top, theme.button_bot)
    handle_on = gel(8, 18, theme.on_top, theme.on_bot)
    im.paste(handle, (52, 53))
    im.paste(handle_on, (61, 53))

    # Menu-button sprites (the corner art is what the window paints; these match).
    menu = [
        (0, 111, "ADD"), (23, 111, "ADD"),
        (0, 130, "DIR"), (23, 130, "DIR"),
        (0, 149, "FILE"), (23, 149, "FILE"),
        (54, 111, "ALL"), (77, 111, "ALL"),
        (54, 130, "CROP"), (77, 130, "CROP"),
        (54, 149, "REM"), (77, 149, "REM"),
        (54, 168, "MISC"), (77, 168, "MISC"),
        (104, 111, "INV"), (127, 111, "INV"),
        (104, 130, "NONE"), (127, 130, "NONE"),
        (104, 149, "ALL"), (127, 149, "ALL"),
        (154, 111, "SORT"), (177, 111, "SORT"),
        (154, 130, "INFO"), (177, 130, "INFO"),
        (154, 149, "OPT"), (177, 149, "OPT"),
        (204, 111, "NEW"), (227, 111, "NEW"),
        (204, 130, "SAVE"), (227, 130, "SAVE"),
        (204, 149, "LOAD"), (227, 149, "LOAD"),
    ]
    for i, (x, y, label) in enumerate(menu):
        down = i % 2 == 1
        im.paste(
            label_button(22, 18, theme.button_top, theme.button_bot, label, theme.button_ink, down),
            (x, y),
        )
    for x, h in ((48, 54), (100, 72), (150, 54), (200, 54), (250, 54)):
        fill(im, x, 111, 3, h, theme.accent)

    im.paste(icon_button(9, 9, theme.button_top, theme.button_bot, CLOSE, theme.hot, True), (52, 42))
    im.paste(icon_button(9, 9, theme.button_top, theme.button_bot, SHADE, theme.hot, True), (62, 42))
    im.paste(icon_button(9, 9, theme.button_top, theme.button_bot, SHADE, theme.hot, True), (150, 42))
    return im


def eq_thumb(theme: Theme, hot: bool) -> Image.Image:
    col = theme.hot if hot else theme.fill
    face = gel(11, 11, shade(col, 0.3), shade(col, -0.35))
    hline(face, 2, 5, 7, theme.on_ink)
    return face


def build_eqmain(theme: Theme) -> Image.Image:
    im = new(275, 315, theme.panel_lo)
    plate = load_plate(theme.key, (275, 116))
    dark = new(275, 116, theme.lcd)
    face = Image.blend(dark, plate, 0.42)
    im.paste(face, (0, 0))
    # dB ticks and band names live on the background; grooves cover the slots.
    draw_text(im, "+12", 46, 33, theme.hot, None, (0, 0, 0))
    draw_text(im, "0", 50, 62, theme.ink, None, (0, 0, 0))
    draw_text(im, "-12", 46, 92, theme.ink, None, (0, 0, 0))
    bands = ["60", "170", "310", "600", "1K", "3K", "6K", "12K", "14K", "16K"]
    for i, label in enumerate(bands):
        cx = 78 + 18 * i + 7
        draw_text(im, label, cx - text_width(label) // 2, 108, theme.ink, None, (0, 0, 0))
    draw_text(im, "PRE", 16, 108, theme.accent, None, (0, 0, 0))

    def eq_title(selected: bool) -> Image.Image:
        top = theme.title_top if selected else shade(theme.title_top, -0.25)
        bot = theme.title_bot if selected else shade(theme.title_bot, -0.35)
        bar = gel(275, 14, top, bot)
        draw_koala(bar, 16, 2, koala_colors(theme.fur, theme.ear, theme.inner, theme.eye, theme.nose))
        draw_text(bar, "KOALAAMP EQ", 30, 4, theme.title_ink if selected else theme.dim)
        bar.paste(icon_button(9, 9, theme.button_top, theme.button_bot, SHADE, theme.button_ink), (254, 3))
        bar.paste(icon_button(9, 9, theme.button_top, theme.button_bot, CLOSE, theme.button_ink), (264, 3))
        if selected:
            hline(bar, 0, 13, 275, theme.accent)
        return bar

    im.paste(eq_title(True), (0, 134))
    im.paste(eq_title(False), (0, 149))
    im.paste(icon_button(9, 9, theme.button_top, theme.button_bot, CLOSE, theme.button_ink), (0, 116))
    im.paste(icon_button(9, 9, theme.button_top, theme.button_bot, CLOSE, theme.hot, True), (0, 125))
    im.paste(icon_button(9, 9, theme.button_top, theme.button_bot, SHADE, theme.hot, True), (254, 152))

    def four(label: str, w: int) -> list[Image.Image]:
        out = []
        for on, down in ((False, False), (True, False), (False, True), (True, True)):
            out.append(
                label_button(
                    w, 12,
                    theme.on_top if on else theme.button_top,
                    theme.on_bot if on else theme.button_bot,
                    label,
                    theme.on_ink if on else theme.button_ink,
                    down,
                )
            )
        return out

    on = four("ON", 26)
    auto = four("AUTO", 32)
    # Idle, selected, depressed, selected+depressed.
    im.paste(on[0], (10, 119))
    im.paste(on[1], (69, 119))
    im.paste(on[2], (128, 119))
    im.paste(on[3], (187, 119))
    im.paste(auto[0], (36, 119))
    im.paste(auto[1], (95, 119))
    im.paste(auto[2], (154, 119))
    im.paste(auto[3], (213, 119))
    im.paste(label_button(44, 12, theme.button_top, theme.button_bot, "PRESETS", theme.button_ink), (224, 164))
    im.paste(label_button(44, 12, theme.button_top, theme.button_bot, "PRESETS", theme.on_ink, True), (224, 176))

    im.paste(eq_thumb(theme, False), (0, 164))
    im.paste(eq_thumb(theme, True), (0, 176))

    for frame in range(28):
        col = frame % 14
        row = frame // 14
        x = 13 + col * 15
        y = 164 + row * 65
        fill(im, x, y, 14, 63, theme.groove)
        bevel(im, x, y, 14, 63, shade(theme.groove, -0.3), shade(theme.groove, 0.25))
        value = frame / 27
        thumb_top = int((51 * (1.0 - value)) // 1)
        fill_y = min(62, thumb_top + 5)
        fill(im, x + 4, y + 31, 6, 1, theme.dim)  # 0 dB tick
        if fill_y < 62:
            fill(im, x + 3, y + fill_y, 8, 62 - fill_y, theme.fill)

    graph = new(113, 19, theme.lcd)
    for yy in range(19):
        hline(graph, 0, yy, 113, mix(theme.lcd, theme.groove, yy / 18))
    hline(graph, 0, 9, 113, shade(theme.dim, 0.2))
    im.paste(graph, (0, 294))
    colors = lerp_n([theme.hot, theme.accent, theme.fill, theme.dim], 19)
    for yy, col in enumerate(colors):
        pixel(im, 115, 294 + yy, col)
    hline(im, 0, 314, 113, theme.hot)
    return im


def build_eq_ex(theme: Theme) -> Image.Image:
    im = new(275, 82, theme.panel_lo)

    def shade_bar(selected: bool) -> Image.Image:
        top = theme.title_top if selected else shade(theme.title_top, -0.25)
        bot = theme.title_bot if selected else shade(theme.title_bot, -0.35)
        bar = gel(275, 14, top, bot)
        draw_text(bar, "KOALA EQ", 8, 4, theme.title_ink if selected else theme.dim)
        inset(bar, 61, 4, 97, 6, theme.groove, theme.panel_hi, theme.panel_lo)
        inset(bar, 164, 4, 43, 6, theme.groove, theme.panel_hi, theme.panel_lo)
        bar.paste(icon_button(9, 9, theme.button_top, theme.button_bot, SHADE, theme.button_ink), (254, 3))
        bar.paste(icon_button(9, 9, theme.button_top, theme.button_bot, CLOSE, theme.button_ink), (264, 3))
        return bar

    im.paste(shade_bar(True), (0, 0))
    im.paste(shade_bar(False), (0, 15))
    for x, col in ((1, theme.dim), (4, theme.fill), (7, theme.hot), (11, theme.dim), (14, theme.fill), (17, theme.hot)):
        thumb = new(3, 7, col)
        bevel(thumb, 0, 0, 3, 7, shade(col, 0.4), shade(col, -0.4))
        im.paste(thumb, (x, 30))
    im.paste(icon_button(9, 9, theme.button_top, theme.button_bot, SHADE, theme.hot, True), (1, 38))
    im.paste(icon_button(9, 9, theme.button_top, theme.button_bot, MINI, theme.hot, True), (1, 47))
    im.paste(icon_button(9, 9, theme.button_top, theme.button_bot, CLOSE, theme.button_ink), (11, 38))
    im.paste(icon_button(9, 9, theme.button_top, theme.button_bot, CLOSE, theme.hot, True), (11, 47))
    return im


def build_gen(theme: Theme) -> Image.Image:
    im = new(194, 109, theme.panel_lo)

    def title(selected: bool) -> Image.Image:
        # Same colour as the bitmap font's cell background, so a gen-window
        # title drawn in text.bmp does not pick up a box around each letter.
        # No side bevel: the 25px fill tile has to repeat cleanly.
        top = shade(theme.lcd, 0.18 if selected else 0.0)
        bot = theme.lcd
        face = new(25, 20, top)
        for yy in range(20):
            fill(face, 0, yy, 25, 1, mix(top, bot, yy / 19))
        return face

    for selected, y in ((True, 0), (False, 21)):
        piece = title(selected)
        for i, x in enumerate((0, 26, 52, 78, 104, 130)):
            im.paste(piece, (x, y))
        # Right cap gets a little accent so the active bar reads as a bar.
        if selected:
            hline(im, 0, y + 19, 155, theme.accent)

    im.paste(gel(125, 14, theme.panel_hi, theme.panel_lo), (0, 42))
    im.paste(gel(125, 14, theme.panel, theme.panel_lo), (0, 57))
    im.paste(gel(25, 14, theme.panel, theme.panel_lo), (127, 72))
    left = gel(11, 29, theme.panel_hi, theme.panel_lo)
    im.paste(left, (127, 42))
    im.paste(gel(11, 24, theme.panel_hi, theme.panel_lo), (158, 42))
    im.paste(gel(8, 29, theme.panel, theme.panel_lo), (139, 42))
    im.paste(gel(8, 24, theme.panel, theme.panel_lo), (170, 42))
    im.paste(icon_button(9, 9, theme.button_top, theme.button_bot, CLOSE, theme.button_ink), (148, 42))
    return im


def viscolor_text(theme: Theme) -> str:
    colors = [theme.lcd, shade(theme.lcd, 0.18)]
    colors += lerp_n([theme.spectrum_hot, theme.spectrum_mid, theme.spectrum_dim], 16)
    colors += lerp_n([theme.hot, theme.dim], 5)
    colors.append(theme.hot)
    assert len(colors) == 24
    return "\n".join(f"{c[0]},{c[1]},{c[2]}" for c in colors) + "\n"


def pledit_text(theme: Theme) -> str:
    return (
        "[Text]\n"
        f"Normal={hex_rgb(theme.pledit_text)}\n"
        f"Current={hex_rgb(theme.pledit_cur)}\n"
        f"NormalBG={hex_rgb(theme.pledit_bg)}\n"
        f"SelectedBG={hex_rgb(theme.pledit_sel)}\n"
        f"MbFG={hex_rgb(theme.pledit_text)}\n"
        f"MbBG={hex_rgb(theme.pledit_bg)}\n"
        "Font=Tahoma\n"
    )


def readme_text(theme: Theme) -> str:
    return (
        f"{theme.filename}\n"
        "An original KoalaAmp classic skin. Early-2000s surface art with the\n"
        "KOALAAMP wordmark drawn into the title bars.\n"
        "Sprite layout follows the classic Winamp 2 sheet coordinates.\n"
        "No Nullsoft artwork is included.\n"
    )


def region_text() -> str:
    return "; KoalaAmp skin. Rectangular window, no mask.\n"


def to_bmp(im: Image.Image) -> bytes:
    buf = io.BytesIO()
    sanitize(im).convert("RGB").save(buf, format="BMP")
    return buf.getvalue()


def build_sheets(theme: Theme) -> dict[str, Image.Image]:
    numbers, nums_ex = build_numbers(theme)
    sheets = {
        "main.bmp": build_main(theme),
        "titlebar.bmp": build_titlebar(theme),
        "cbuttons.bmp": build_cbuttons(theme),
        "posbar.bmp": build_posbar(theme),
        "volume.bmp": build_volume(theme),
        "balance.bmp": build_balance(theme),
        "shufrep.bmp": build_shufrep(theme),
        "monoster.bmp": build_monoster(theme),
        "playpaus.bmp": build_playpaus(theme),
        "numbers.bmp": numbers,
        "nums_ex.bmp": nums_ex,
        "text.bmp": build_text(theme),
        "pledit.bmp": build_pledit(theme),
        "eqmain.bmp": build_eqmain(theme),
        "eq_ex.bmp": build_eq_ex(theme),
        "gen.bmp": build_gen(theme),
    }
    for name, im in sheets.items():
        need = MIN_SIZE[name]
        if im.size[0] < need[0] or im.size[1] < need[1]:
            raise SystemExit(f"{theme.filename} {name} is {im.size}, need at least {need}")
    return sheets


def write_wsz(theme: Theme, sheets: dict[str, Image.Image]) -> Path:
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / theme.filename
    texts = {
        "viscolor.txt": viscolor_text(theme),
        "pledit.txt": pledit_text(theme),
        "region.txt": region_text(),
        "readme.txt": readme_text(theme),
    }
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name, im in sheets.items():
            zf.writestr(name, to_bmp(im))
        for name, text in texts.items():
            zf.writestr(name, text.encode("latin-1"))
    return path


# --- previews (EQ + playlist; the main window is also rendered by spike_fe3) ---

def scale_nn(im: Image.Image, factor: int) -> Image.Image:
    return im.resize((im.width * factor, im.height * factor), Image.Resampling.NEAREST)


def preview_eq(theme: Theme, sheets: dict[str, Image.Image]) -> Image.Image:
    im = new(275, 116, (0, 0, 0))
    im.paste(sheets["eqmain.bmp"].crop((0, 0, 275, 116)), (0, 0))
    im.paste(sheets["eqmain.bmp"].crop((0, 134, 275, 148)), (0, 0))
    im.paste(sheets["eqmain.bmp"].crop((69, 119, 95, 131)), (14, 18))  # ON selected
    im.paste(sheets["eqmain.bmp"].crop((36, 119, 68, 131)), (40, 18))
    im.paste(sheets["eqmain.bmp"].crop((224, 164, 268, 176)), (217, 18))
    im.paste(sheets["eqmain.bmp"].crop((0, 294, 113, 313)), (86, 17))
    # Smile curve, AU values (0 = +12 dB).
    aus = [31, 8, 14, 20, 26, 31, 31, 22, 14, 8, 6]
    for i, au in enumerate(aus):
        web = round((63 - au) * 100 / 63)
        frame = max(0, min(27, round(web / 100 * 27)))
        col = frame % 14
        row = frame // 14
        src = (13 + col * 15, 164 + row * 65, 13 + col * 15 + 14, 164 + row * 65 + 63)
        x = 21 if i == 0 else 78 + 18 * (i - 1)
        im.paste(sheets["eqmain.bmp"].crop(src), (x, 38))
        thumb_top = int(51 * (1 - web / 100))
        im.paste(sheets["eqmain.bmp"].crop((0, 164, 11, 175)), (x + 1, 38 + thumb_top))
    return im


def preview_playlist(theme: Theme, sheets: dict[str, Image.Image], extra: int) -> Image.Image:
    width = 275 + extra * 25
    height = 116
    im = new(width, height, theme.pledit_bg)
    pledit = sheets["pledit.bmp"]
    # Title band.
    im.paste(pledit.crop((127, 0, 152, 20)), (25, 0))
    # Tile the top between the corners. The 25px tile is uniform enough to repeat.
    tile = pledit.crop((127, 0, 152, 20))
    for x in range(25, width - 25, 25):
        im.paste(tile, (x, 0))
    im.paste(pledit.crop((0, 0, 25, 20)), (0, 0))
    title_x = 25 + (12 if extra % 2 == 0 else 0)
    # Roughly centered title, good enough for a contact sheet.
    title_x = max(25, (width - 100) // 2)
    im.paste(pledit.crop((26, 0, 126, 20)), (title_x, 0))
    im.paste(pledit.crop((153, 0, 178, 20)), (width - 25, 0))
    # Sides.
    left = pledit.crop((0, 42, 12, 71))
    right = pledit.crop((31, 42, 51, 71))
    for y in range(20, height - 38, 29):
        im.paste(left, (0, y))
        im.paste(right, (width - 20, y))
    im.paste(pledit.crop((0, 72, 125, 110)), (0, height - 38))
    im.paste(pledit.crop((126, 72, 276, 110)), (width - 150, height - 38))
    if extra >= 3:
        im.paste(pledit.crop((205, 0, 280, 38)), (width - 225, height - 38))
        # Corners paint after the vis in the real renderer and cover its edges
        # only when the window is narrow. Repaint them so this matches.
        im.paste(pledit.crop((0, 72, 125, 110)), (0, height - 38))
        im.paste(pledit.crop((126, 72, 276, 110)), (width - 150, height - 38))
    return im


THEMES = [
    Theme(
        key="aqua",
        filename="KoalaAmp-Aqua.wsz",
        title="KOALAAMP",
        egg="CHEW LOUDER",
        panel=(36, 140, 186),
        panel_hi=(210, 242, 250),
        panel_lo=(10, 70, 110),
        lcd=(4, 28, 44),
        ink=(214, 246, 255),
        dim=(90, 150, 170),
        accent=(255, 255, 255),
        accent2=(120, 230, 255),
        hot=(230, 255, 255),
        groove=(2, 18, 32),
        fill=(60, 210, 255),
        title_top=(20, 110, 168),
        title_bot=(6, 58, 110),
        title_ink=(240, 252, 255),
        button_top=(232, 248, 255),
        button_bot=(70, 160, 200),
        button_ink=(8, 48, 84),
        on_top=(170, 245, 255),
        on_bot=(20, 120, 180),
        on_ink=(0, 40, 70),
        fur=(196, 202, 208),
        ear=(90, 100, 112),
        inner=(240, 170, 180),
        nose=(24, 24, 28),
        eye=(250, 250, 252),
        spectrum_hot=(230, 255, 255),
        spectrum_mid=(40, 190, 255),
        spectrum_dim=(8, 50, 90),
        pledit_bg=(4, 28, 44),
        pledit_sel=(10, 90, 140),
        pledit_text=(214, 246, 255),
        pledit_cur=(255, 255, 255),
    ),
    Theme(
        key="laser",
        filename="KoalaAmp-Laser.wsz",
        title="KOALAAMP",
        egg="CHEW LOUDER",
        panel=(8, 6, 20),
        panel_hi=(40, 255, 255),
        panel_lo=(0, 0, 0),
        lcd=(0, 0, 0),
        ink=(140, 255, 255),
        dim=(50, 70, 110),
        accent=(255, 40, 210),
        accent2=(0, 255, 240),
        hot=(255, 60, 220),
        groove=(0, 0, 8),
        fill=(0, 255, 230),
        title_top=(16, 0, 28),
        title_bot=(0, 0, 0),
        title_ink=(140, 255, 255),
        button_top=(24, 16, 48),
        button_bot=(0, 0, 0),
        button_ink=(180, 255, 255),
        on_top=(255, 40, 200),
        on_bot=(80, 0, 70),
        on_ink=(255, 255, 255),
        fur=(20, 16, 32),
        ear=(0, 220, 230),
        inner=(255, 40, 200),
        nose=(0, 0, 0),
        eye=(255, 40, 210),
        spectrum_hot=(255, 60, 220),
        spectrum_mid=(40, 80, 255),
        spectrum_dim=(0, 40, 50),
        pledit_bg=(0, 0, 0),
        pledit_sel=(40, 0, 48),
        pledit_text=(140, 255, 255),
        pledit_cur=(255, 60, 220),
    ),
    Theme(
        key="riot",
        filename="KoalaAmp-Riot.wsz",
        title="KOALAAMP",
        egg="CHEW LOUDER",
        panel=(48, 46, 48),
        panel_hi=(180, 176, 170),
        panel_lo=(12, 10, 10),
        lcd=(16, 6, 6),
        ink=(255, 220, 210),
        dim=(140, 120, 116),
        accent=(255, 36, 28),
        accent2=(240, 180, 40),
        hot=(255, 70, 48),
        groove=(12, 4, 4),
        fill=(255, 32, 24),
        title_top=(70, 64, 64),
        title_bot=(16, 12, 12),
        title_ink=(255, 230, 220),
        button_top=(120, 116, 114),
        button_bot=(28, 26, 26),
        button_ink=(255, 236, 230),
        on_top=(255, 48, 32),
        on_bot=(90, 8, 6),
        on_ink=(255, 240, 220),
        fur=(180, 180, 184),
        ear=(70, 70, 74),
        inner=(210, 210, 214),
        nose=(20, 20, 22),
        eye=(250, 250, 250),
        spectrum_hot=(255, 220, 80),
        spectrum_mid=(255, 40, 24),
        spectrum_dim=(60, 0, 0),
        pledit_bg=(16, 6, 6),
        pledit_sel=(90, 12, 10),
        pledit_text=(255, 220, 210),
        pledit_cur=(255, 70, 48),
    ),
    Theme(
        key="starz",
        filename="KoalaAmp-Starz.wsz",
        title="KOALAAMP",
        egg="CHEW LOUDER",
        panel=(190, 30, 140),
        panel_hi=(255, 180, 230),
        panel_lo=(80, 8, 70),
        lcd=(42, 0, 36),
        ink=(255, 245, 255),
        dim=(230, 140, 190),
        accent=(255, 236, 70),
        accent2=(180, 255, 255),
        hot=(255, 255, 255),
        groove=(32, 0, 28),
        fill=(255, 80, 200),
        title_top=(210, 40, 150),
        title_bot=(90, 0, 70),
        title_ink=(255, 255, 255),
        button_top=(255, 150, 210),
        button_bot=(150, 20, 120),
        button_ink=(60, 0, 40),
        on_top=(255, 240, 120),
        on_bot=(200, 80, 20),
        on_ink=(60, 0, 40),
        fur=(190, 190, 198),
        ear=(80, 70, 90),
        inner=(255, 150, 190),
        nose=(30, 30, 36),
        eye=(255, 255, 255),
        spectrum_hot=(255, 255, 255),
        spectrum_mid=(255, 80, 200),
        spectrum_dim=(80, 0, 90),
        pledit_bg=(42, 0, 36),
        pledit_sel=(140, 20, 100),
        pledit_text=(255, 245, 255),
        pledit_cur=(255, 236, 70),
    ),
]


def main() -> None:
    PREVIEW.mkdir(parents=True, exist_ok=True)
    for theme in THEMES:
        sheets = build_sheets(theme)
        path = write_wsz(theme, sheets)
        scale_nn(sheets["main.bmp"], 3).save(PREVIEW / f"{theme.key}-plate.png")
        scale_nn(preview_eq(theme, sheets), 3).save(PREVIEW / f"{theme.key}-eq.png")
        scale_nn(preview_playlist(theme, sheets, 0), 3).save(PREVIEW / f"{theme.key}-pl.png")
        scale_nn(preview_playlist(theme, sheets, 3), 3).save(PREVIEW / f"{theme.key}-pl-wide.png")
        print(f"wrote {path} ({path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
