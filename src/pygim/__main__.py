# -*- coding: utf-8 -*-
"""
Python Gimmicks Command-Line Interface.
"""

import sys

import click
from _pygim._cli._banner import BannerGroup
from _pygim._cli._cli_app import GimmicksCliApp, flag_opt


@click.group(cls=BannerGroup, tagline="Python Gimmicks")
def cli():
    pass


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


class _OoGroup(BannerGroup):
    """`oo <verb> ...` runs a verb; nothing at all shows the help; anything else is an error.

    An unknown word used to be taken as free text for an assistant that was never built, which
    printed a sentence and exited 0, so a mistyped or renamed command reported success and did
    nothing. A command that does not exist has to say so, and say it with a status a script can see."""

    def resolve_command(self, ctx, args):
        name = click.utils.make_str(args[0]) if args else ""
        if args and not name.startswith("-") and self.get_command(ctx, name) is None:
            import difflib

            near = difflib.get_close_matches(name, self.list_commands(ctx), n=1, cutoff=0.6)
            ctx.fail(f"No such command {name!r}." + (f" Did you mean {near[0]!r}?" if near else "")
                     + " Run `oo --help` for the list.")
        return super().resolve_command(ctx, args)


@click.group(cls=_OoGroup, invoke_without_command=True, tagline="AI powered Python Gimmicks")
@flag_opt("--no-color", "no_color", help="Never colour the output. Colour is already off when the output is "
                                         "not a terminal, when NO_COLOR is set, or when TERM is dumb.")
@click.pass_context
def cli_oo(ctx, no_color):
    """The composition root for the commands: the environment is read once, here, and every command
    is handed the result. Nothing below reads it again (see `_pygim._config`)."""
    from _pygim import _config
    from _pygim._cli import _style

    ctx.obj = _config.from_process(colour=False if no_color else None)
    _style.use(ctx.obj.colour)
    if ctx.invoked_subcommand is None:
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


if __name__ == "__main__":
    # `python -m pygim` runs what the `pygim` script runs; `oo` is the other entry point.
    cli()
