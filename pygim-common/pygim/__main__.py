# -*- coding: utf-8 -*-
"""
Python Gimmicks Command-Line Interface.
"""

import click
from _pygim._cli.cliapp import GimmicksCliApp

from _pygim._cli import flag_opt


@click.group()
def cli():
    """\b
     ___       ___ _
    | _ \_  _ / __(_)_ __
    |  _/ || | (_ | | '  \ \b
    |_|  \_, |\___|_|_|_|_|
        |__/Python Gimmicks

    """


@cli.command()
@flag_opt("-y", "--yes",            help="Confirm the action without prompting.")
@flag_opt("-q", "--quiet",          help="Sssh! No output!")
@flag_opt("-p", "--pycache-dirs",   help="Remove all __pycache__ folders.")
@flag_opt("-b", "--build-dirs",     help="Remove any and all build folders.")
@flag_opt("-c", "--compiled-files", help="Remove compiled files")
@flag_opt("-a", "--all",            help="Remove all extra files or folders.")
def clean_up(**kwargs):
    """ Remove unnecessary files and folders related to Python. """
    GimmicksCliApp().clean_up(**kwargs)


@cli.command()
def show_test_coverage(**kwargs):
    """ Run test coverage in current folder. """
    GimmicksCliApp().show_test_coverage(**kwargs)
