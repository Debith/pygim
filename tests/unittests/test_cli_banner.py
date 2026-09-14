# -*- coding: utf-8 -*-
"""The CLI banners reach the help output verbatim: click rewraps help text
unless the paragraph is marked with a real backspace line."""

import pytest
from click.testing import CliRunner

from pygim.__main__ import cli, cli_oo

PYGIM = [  # figlet -f small PyGim
    " ___       ___ _",
    "| _ \\_  _ / __(_)_ __",
    "|  _/ || | (_ | | '  \\",
    "|_|  \\_, |\\___|_|_|_|_|",
]


@pytest.mark.parametrize("command,tagline", [
    (cli, "Python Gimmicks"),
    (cli_oo, "AI powered Python Gimmicks"),
])
def test_banner_is_printed_line_for_line(command, tagline):
    result = CliRunner().invoke(command, ["--help"])
    assert result.exit_code == 0, result.output
    lines = [line[2:] for line in result.output.splitlines()]  # click indents help by two
    start = lines.index(PYGIM[0])
    assert lines[start:start + 5] == [*PYGIM, f"     |__/ {tagline}"]
    assert "\\b" not in result.output
