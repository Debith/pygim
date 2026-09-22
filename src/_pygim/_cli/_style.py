# -*- coding: utf-8 -*-
"""How the command line spends attention: a title, an indent, a blank line, and two colours.

Colour is conditional output, not decoration. It is off when the output is not a terminal (click
strips styles itself then), when ``NO_COLOR`` is set to any value, when ``TERM`` is ``dumb``, and
when ``oo --no-color`` was passed. And it is never the only signal: every coloured thing here also
carries a word — *refused*, *accepted*, *waiting* — because about one man in twelve cannot tell
red from green (WCAG 2.2, Use of Color).

Two colours, with intent: red for something that went wrong or needs an answer, green for a change
that happened. Everything else is weight — **bold** for what a reader should land on, dim for the
identifiers they only need when they act. Rules from clig.dev, Output.
"""
from __future__ import annotations

import os

import click

NO_COLOR = "PYGIM_NO_COLOR"


def enabled() -> bool:
    """Whether to emit colour at all. click removes it again when the stream is not a terminal."""
    if os.environ.get("NO_COLOR") is not None or os.environ.get(NO_COLOR) is not None:
        return False
    return os.environ.get("TERM") != "dumb"


def off() -> None:
    """Turn colour off for this process — what ``oo --no-color`` does."""
    os.environ[NO_COLOR] = "1"


def _paint(text: str, **style: object) -> str:
    return click.style(text, **style) if enabled() else text


def title(text: str) -> str:
    """A heading a reader lands on. Weight, not colour: it must survive a monochrome terminal."""
    return _paint(text, bold=True)


def strong(text: str) -> str:
    """The one thing on the line worth acting on — a count, a name, a verdict."""
    return _paint(text, bold=True)


def muted(text: str) -> str:
    """An identifier, a path, a timestamp: needed when acting, noise when reading."""
    return _paint(text, dim=True)


def good(text: str) -> str:
    """Something changed, and it worked. Always beside a word that says so."""
    return _paint(text, fg="green")


def bad(text: str) -> str:
    """A refusal, a failure, or a question waiting for an answer."""
    return _paint(text, fg="red")
