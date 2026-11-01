# -*- coding: utf-8 -*-
"""The CLI banners reach the help output verbatim: click rewraps help text
unless the paragraph is marked with a real backspace line."""

import pytest
from click.testing import CliRunner

from pygim.__main__ import cli, cli_oo

PYGIM = [  # figlet -f "ANSI Shadow" PYGIM
    "██████╗ ██╗   ██╗ ██████╗ ██╗███╗   ███╗",
    "██╔══██╗╚██╗ ██╔╝██╔════╝ ██║████╗ ████║",
    "██████╔╝ ╚████╔╝ ██║  ███╗██║██╔████╔██║",
    "██╔═══╝   ╚██╔╝  ██║   ██║██║██║╚██╔╝██║",
    "██║        ██║   ╚██████╔╝██║██║ ╚═╝ ██║",
    "╚═╝        ╚═╝    ╚═════╝ ╚═╝╚═╝     ╚═╝",
]


@pytest.mark.parametrize("command,tagline", [
    (cli, "Python Gimmicks"),
    (cli_oo, "AI powered Python Gimmicks"),
])
def test_banner_is_printed_line_for_line(command, tagline):
    result = CliRunner().invoke(command, ["--help"])
    assert result.exit_code == 0, result.output
    lines = result.output.splitlines()
    start = next(i for i, line in enumerate(lines) if line.endswith(PYGIM[0]))
    column = len(lines[start]) - len(PYGIM[0])  # the snake sits to the left
    name = [line[column:] for line in lines[start:start + 7]]
    assert name == [*PYGIM, tagline.rjust(len(PYGIM[0]))]
    assert "\\b" not in result.output
    assert "\x1b" not in result.output  # colours only reach a terminal


def _wedges(text):
    """The Symbols for Legacy Computing diagonals in *text*."""
    return {char for char in text if 0x1FB3C <= ord(char) < 0x1FB68}


def test_snake_corners_are_rounded_only_where_colours_reach_a_terminal():
    import click

    coloured = CliRunner().invoke(cli_oo, ["--help"], color=True).output
    plain = CliRunner().invoke(cli_oo, ["--help"]).output
    assert _wedges(coloured)
    assert not _wedges(plain)
    # rounding changes corners only: the name and tagline are the same either way
    assert click.unstyle(coloured).splitlines()[2][-len(PYGIM[0]):] == PYGIM[0]
