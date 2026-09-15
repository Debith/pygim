# -*- coding: utf-8 -*-
"""
The PyGim help banner: the Python snake pressing a barbell, beside the name.

The snake is pixel art. A terminal cell is about twice as tall as it is wide,
so each character holds two pixels, drawn with half blocks: the upper pixel
in the foreground colour of an ``▀`` and the lower one in its background.
When click strips the colours (the output is not a terminal) the half blocks
still leave the silhouette.
"""

import click

__all__ = ["banner"]

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


def _cell(upper, lower):
    """One character showing the *upper* and *lower* pixels."""
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


def _pixel_lines(pixels):
    """The pixel rows, two to a line of text."""
    return ["".join(map(_cell, upper, lower)) for upper, lower in zip(pixels[::2], pixels[1::2])]


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


def banner(tagline):
    """Help text: the snake, the name and *tagline* right-aligned beneath it.

    Click rewraps help text unless a paragraph starts with a line holding only
    \\b (the backspace character), so this goes to click as help=, where a raw
    docstring would carry a literal backslash and b. The paragraph must hold no
    blank line.
    """
    name = [*map(_paint_name, _PYGIM), click.style(tagline.rjust(len(_PYGIM[0])), italic=True)]
    return "\n".join(["\b", *(f"{snake}   {text}" for snake, text in zip(_pixel_lines(_SNAKE), name))])
