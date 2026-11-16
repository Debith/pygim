# -*- coding: utf-8 -*-
"""
The PyGim help banner: the Python snake pressing a barbell, beside the name.

The snake is pixel art. A terminal cell is about twice as tall as it is wide,
so each character holds two pixels, drawn with half blocks: the upper pixel
in the foreground colour of an ``▀`` and the lower one in its background.

Where the colours reach a terminal, the outline is smoothed too: corners are
cut into bevels, and each cell at the edge takes the glyph from Unicode's
Symbols for Legacy Computing whose diagonal fits that shape best.
Elsewhere (a pipe, a file) the half blocks stay, as they render in any font.
"""

import sys
import unicodedata
from functools import lru_cache
from math import floor

import click

__all__ = ["BannerGroup"]

_PALETTE = {
    "B": (55, 118, 171),   # Python blue
    "b": (35, 80, 125),    # its shade
    "Y": (255, 212, 59),   # Python yellow
    "y": (200, 160, 30),   # its shade
    "G": (170, 170, 170),  # the bar
    "W": (255, 255, 255),  # the eye
    "K": (20, 20, 20),     # its pupil
    "R": (230, 50, 60),    # sweatband and tongue
    "S": (120, 200, 255),  # sweat
}

# One character per pixel, "." for none. The bar bends under the plates;
# corners are rounded by leaving their pixel out.
_SNAKE = (
    ".YYy....................yYY.",
    "YYYy....................yYYY",
    "YYYy....GGGGGGGGGGGG....yYYY",
    "YYYyGGGG..BBBBBBBBb.GGGGyYYY",
    "YYYy.....RRRRRRRRRRR....yYYY",
    ".YYy..S..BBBBBBBWWBb..R.yYY.",
    ".........BBBBBBBWKBbRR......",
    ".....S...BBBBBBBBBBb..R.....",
    "..........BBBBBBBBb.........",
    "...........BBBBBb...........",
    "........BBBBBBBBb...........",
    ".......BBBBBBBb.............",
    "......BBBb..................",
    ".......BBBBBBBBBBBBBBBBbb...",
)

# "PYGIM" in figlet's ANSI Shadow font.
_PYGIM = (
    "██████╗ ██╗   ██╗ ██████╗ ██╗███╗   ███╗",
    "██╔══██╗╚██╗ ██╔╝██╔════╝ ██║████╗ ████║",
    "██████╔╝ ╚████╔╝ ██║  ███╗██║██╔████╔██║",
    "██╔═══╝   ╚██╔╝  ██║   ██║██║██║╚██╔╝██║",
    "██║        ██║   ╚██████╔╝██║██║ ╚═╝ ██║",
    "╚═╝        ╚═╝    ╚═════╝ ╚═╝╚═╝     ╚═╝",
)
_FROM, _TO = (0, 215, 255), (215, 0, 255)  # cyan -> magenta, left to right


# Samples per cell when fitting a glyph; a cell is twice as tall as wide.
_ACROSS, _DOWN = 6, 12

# The points the diagonal glyphs are named by, on the cell's 2x3 sextant grid.
_POINTS = {
    "UPPER LEFT": (0, 0), "UPPER CENTRE": (0.5, 0), "UPPER RIGHT": (1, 0),
    "UPPER MIDDLE LEFT": (0, 1 / 3), "UPPER MIDDLE RIGHT": (1, 1 / 3),
    "LOWER MIDDLE LEFT": (0, 2 / 3), "LOWER MIDDLE RIGHT": (1, 2 / 3),
    "LOWER LEFT": (0, 1), "LOWER CENTRE": (0.5, 1), "LOWER RIGHT": (1, 1),
}


def _samples():
    """Where a cell is sampled, as fractions of its width and height."""
    return [((i + 0.5) / _ACROSS, (j + 0.5) / _DOWN) for j in range(_DOWN) for i in range(_ACROSS)]


def _mask(inside):
    """The samples *inside* covers, one bit each."""
    return sum(1 << bit for bit, (x, y) in enumerate(_samples()) if inside(x, y))


def _corner_side(a, b, corner):
    """Inside: on the same side of the line a-b as *corner*."""
    def side(x, y):
        return (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0])
    here = side(*corner) > 0
    return lambda x, y: (side(x, y) > 0) == here


@lru_cache(maxsize=None)
def _glyphs():
    """Each glyph a cell may take, with the samples it covers. The plain blocks
    come first, so a tie keeps them."""
    shapes = {" ": lambda x, y: False, "█": lambda x, y: True,
              "▀": lambda x, y: y < 0.5, "▄": lambda x, y: y >= 0.5}
    for code in range(0x1FB3C, 0x1FB68):  # LOWER LEFT BLOCK DIAGONAL UPPER LEFT TO LOWER CENTRE, ...
        char = chr(code)
        corner, _, line = unicodedata.name(char).partition(" BLOCK DIAGONAL ")
        a, b = (_POINTS[point] for point in line.split(" TO "))
        shapes[char] = _corner_side(a, b, _POINTS[corner])
    return [(char, _mask(inside)) for char, inside in shapes.items()]


def _half_block(upper, lower):
    """One character showing the *upper* and *lower* pixels as they are."""
    upper, lower = _PALETTE.get(upper), _PALETTE.get(lower)
    if upper is None and lower is None:
        return " "
    if upper == lower:
        return click.style("█", fg=upper)
    if lower is None:
        return click.style("▀", fg=upper)
    if upper is None:
        return click.style("▄", fg=lower)
    return click.style("▀", fg=upper, bg=lower)


class _Smoothed:
    """The pixel art with its corners cut, sampled a cell at a time.

    A filled pixel at an outer corner of a mass (empty on two adjacent sides,
    filled on the two opposite ones) loses the triangle towards that corner,
    and an empty pixel at an inner corner gains one. The triangle is half a cell
    wide and a third of a cell tall, as the diagonal glyphs are drawn, so only a
    corner that meets the glyphs' grid is rounded; the rest stay square."""

    _CORNERS = ((-1, -1), (-1, 1), (1, -1), (1, 1))  # (row, column) towards the corner
    _WIDTH, _HEIGHT = 0.5, 1 / 3  # the triangle's legs, in cells
    _LARGEST_CHANGE = _ACROSS * _DOWN // 6  # samples a glyph may differ from the half blocks by

    def __init__(self, pixels):
        self._pixels = pixels

    def _pixel(self, row, column):
        inside = 0 <= row < len(self._pixels) and 0 <= column < len(self._pixels[row])
        return self._pixels[row][column] if inside and self._pixels[row][column] in _PALETTE else None

    def _corner(self, row, column):
        """The corner (dy, dx) where the pixel is cut or gains a triangle, and
        the colour it then shows; None when it has no such corner."""
        pixel = self._pixel(row, column)
        for dy, dx in self._CORNERS:
            towards = self._pixel(row + dy, column), self._pixel(row, column + dx)
            away = self._pixel(row - dy, column), self._pixel(row, column - dx)
            if pixel and not any(towards) and all(away):
                return dy, dx, None
            if not pixel and all(towards) and towards[0] == towards[1] and not any(away):
                return dy, dx, towards[0]
        return None

    def _inside(self, x, y):
        row, column = floor(y), floor(x)
        filled = self._pixel(row, column) is not None
        corner = self._corner(row, column)
        if corner is None:
            return filled
        dy, dx, _ = corner
        fx, fy = x - column, y - row
        along = (fx if dx < 0 else 1 - fx) / self._WIDTH + (fy if dy < 0 else 1 - fy) / 2 / self._HEIGHT
        return filled != (along < 1)

    def _colour(self, line, column):
        """The one colour of the cell, or the colour of the triangle an empty
        cell gains; None when it holds two, since a glyph has no background."""
        upper, lower = self._pixel(2 * line, column), self._pixel(2 * line + 1, column)
        if upper and lower and upper != lower:
            return None
        if upper or lower:
            return upper or lower
        corners = [self._corner(row, column) for row in (2 * line, 2 * line + 1)]
        return next((corner[2] for corner in corners if corner), None)

    def cell(self, line, column):
        upper, lower = self._pixels[2 * line][column], self._pixels[2 * line + 1][column]
        plain = _half_block(upper, lower)
        colour = self._colour(line, column)
        if colour is None:
            return plain
        wanted = _mask(lambda x, y: self._inside(column + x, 2 * (line + y)))
        glyphs = _glyphs()
        as_is = next(mask for char, mask in glyphs if char == click.unstyle(plain))
        char, mask = min(glyphs, key=lambda glyph: bin(glyph[1] ^ wanted).count("1"))
        if bin(mask ^ wanted).count("1") >= bin(as_is ^ wanted).count("1") or \
                bin(mask ^ as_is).count("1") > self._LARGEST_CHANGE:
            return plain
        return click.style(char, fg=_PALETTE[colour])


def _pixel_lines(pixels, smooth):
    """The pixel rows, two to a line of text."""
    if smooth:
        shape = _Smoothed(pixels)
        return ["".join(shape.cell(line, column) for column in range(len(pixels[0])))
                for line in range(len(pixels) // 2)]
    return ["".join(map(_half_block, upper, lower)) for upper, lower in zip(pixels[::2], pixels[1::2])]


def _paint_name(line):
    """Blocks in a horizontal gradient, the shadow dimmed."""
    width = len(_PYGIM[0]) - 1
    out = []
    for column, char in enumerate(line):
        if char == " ":
            out.append(char)
        elif char == "█":
            t = column / width
            rgb = tuple(round(a + (b - a) * t) for a, b in zip(_FROM, _TO))
            out.append(click.style(char, fg=rgb, bold=True))
        else:
            out.append(click.style(char, fg="bright_black"))
    return "".join(out)


def _banner(tagline, smooth):
    """The snake, the name and *tagline* right-aligned beneath it.

    Click rewraps help text unless a paragraph starts with a line holding only
    \\b (the backspace character). The paragraph must hold no blank line.
    """
    name = [*map(_paint_name, _PYGIM), click.style(tagline.rjust(len(_PYGIM[0])), italic=True)]
    snake = _pixel_lines(_SNAKE, smooth)
    return "\n".join(["\b", *(f"{art}   {text}" for art, text in zip(snake, name))])


class BannerGroup(click.Group):
    """A command group whose help opens with the banner and *tagline*.

    The banner is drawn when the help is, because only then is it known whether
    the colours reach a terminal, and so whether the smoothed glyphs will."""

    def __init__(self, *args, tagline, **kwargs):
        super().__init__(*args, **kwargs)
        self.tagline = tagline

    def format_help_text(self, ctx, formatter):
        smooth = not click.utils.should_strip_ansi(sys.stdout, ctx.color)
        formatter.write_paragraph()
        with formatter.indentation():
            formatter.write_text(_banner(self.tagline, smooth))
