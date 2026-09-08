# -*- coding: utf-8 -*-
"""
Python Gimmicks Command-Line Interface.
"""

import click
from _pygim._cli._cli_app import GimmicksCliApp, flag_opt


@click.group()
def cli():
    r"""\b
     ___        ___ _
    | _ \_  _  / __(_)\_ __
    |  _/ || || (_ | | '  \ \b
    |_|  \_, / \___|_|_|_|_|
        |_/Python Gimmicks

    """


@cli.command()
@flag_opt("-y", "--yes", help="Confirm the action without prompting.")
@flag_opt("-q", "--quiet", help="Sssh! No output!")
@flag_opt("-p", "--pycache-dirs", help="Remove all __pycache__ folders.")
@flag_opt("-b", "--build-dirs", help="Remove any and all build folders.")
@flag_opt("-c", "--compiled-files", help="Remove compiled files")
@flag_opt("-a", "--all", help="Remove all extra files or folders.")
def clean_up(**kwargs):
    """Remove unnecessary files and folders related to Python."""
    GimmicksCliApp().clean_up(**kwargs)


@cli.command()
def show_test_coverage(**kwargs):
    """Run test coverage in current folder."""
    GimmicksCliApp().show_test_coverage(**kwargs)


@cli.command()
def show_support():
    """Display compiled feature support matrix."""
    GimmicksCliApp().show_support()


@cli.command()
@flag_opt("--check", help="Report whether the stub is current instead of rewriting it (exit 1 if stale).")
def stubs(check):
    """Regenerate the engine block of pygim/pathlike.pyi from the built module.

    pathlike's engines are discovered by the build (one header each), so the
    stub's Engine literal and <name>file classes are derived, not hand-written."""
    GimmicksCliApp().stubs(check=check)


class _OoGroup(click.Group):
    """`oo <verb> ...` runs a verb (``docs serve``); anything else is free text
    for the assistant; nothing at all shows the help."""

    def parse_args(self, ctx, args):
        if args and not args[0].startswith("-") and args[0] not in self.commands:
            ctx.meta["free_text"] = " ".join(args)
            ctx.args = []
            return []
        return super().parse_args(ctx, args)

    def invoke(self, ctx):
        text = ctx.meta.get("free_text")
        if text is not None:
            return GimmicksCliApp().ai(text)
        return super().invoke(ctx)


@click.group(cls=_OoGroup, invoke_without_command=True)
@click.pass_context
def cli_oo(ctx):
    r"""\b
     ___        ___ _
    | _ \_  _  / __(_)\_ __
    |  _/ || || (_ | | '  \ \b
    |_|  \_, / \___|_|_|_|_|
        |_/ AI powered Python Gimmicks

    """
    if ctx.invoked_subcommand is None and ctx.meta.get("free_text") is None:
        click.echo(ctx.get_help())


@cli_oo.group()
def docs():
    """Documentation tools."""


@docs.command("serve")
@click.option("--port", type=int, default=8000, show_default=True, help="Port to listen on.")
@click.option("--dir", "directory", type=click.Path(exists=True, file_okay=False), default=None,
              help="The docs directory to serve (default: the current directory).")
@click.option("--host", default=None,
              help="Interface to bind (default: all interfaces, or $PYGIM_HOST; use 127.0.0.1 for localhost only).")
@click.option("--index", default=None,
              help="Root-relative page `/` redirects to (default: the root's index.html, else the first of "
                   "site/, docs/, build/html/, docs/_build/html/ that has one).")
@click.option("--rebuild", default=None,
              help="A shell command run in the served directory before serving; a non-zero exit aborts.")
def docs_serve(port, directory, host, index, rebuild):
    """Serve a docs directory with the review layer: every HTML page gets the
    commenter (comments land in __notes__/site-comments.jsonl under the served
    root) and images dropped on a page are written under images/."""
    GimmicksCliApp().docs_serve(port=port, directory=directory, host=host, index=index, rebuild=rebuild)
