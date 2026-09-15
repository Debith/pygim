# -*- coding: utf-8 -*-
"""
Python Gimmicks Command-Line Interface.
"""

import click
from _pygim._cli._cli_app import GimmicksCliApp, flag_opt


# "PyGim" in figlet's small font. Click rewraps help text unless a paragraph
# starts with a line holding only \b (the backspace character), so the banner
# is passed as help= with a real one; a raw docstring would carry a literal
# backslash and b instead.
_PYGIM = r"""
 ___       ___ _
| _ \_  _ / __(_)_ __
|  _/ || | (_ | | '  \
|_|  \_, |\___|_|_|_|_|
     |__/ """


def _banner(tagline):
    """Help text: the PyGim banner with *tagline*, kept verbatim by click."""
    return "\b" + _PYGIM + tagline


@click.group(help=_banner("Python Gimmicks"))
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


@click.group(cls=_OoGroup, invoke_without_command=True, help=_banner("AI powered Python Gimmicks"))
@click.pass_context
def cli_oo(ctx):
    if ctx.invoked_subcommand is None and ctx.meta.get("free_text") is None:
        click.echo(ctx.get_help())


@cli_oo.group()
def memory():
    """Problem-space memory: retrieval by the kind of problem being solved."""


_ROOT = click.option("--root", default=None, type=click.Path(file_okay=False),
                     help="The memory store. Default: $PYGIM_MEMORY_ROOT, then `git config pygim.memory` "
                          "(shared by every worktree), then a .memory above the working directory.")


@memory.command("setup")
@click.option("--user", "kind", flag_value="user", help="Create the store in your user data directory.")
@click.option("--branch", "kind", flag_value="branch",
              help="Keep the store on an orphan `memory` branch, checked out as a worktree of its own.")
@click.option("--name", default=None, help="--user: the store's name (default: the project directory's name).")
@click.option("--path", "path", default=None, type=click.Path(file_okay=False),
              help="--branch: where to check the branch out (default: beside the main worktree).")
@click.option("--from", "source", default=None, type=click.Path(exists=True, file_okay=False),
              help="Start the new store as a copy of an existing one, such as a project's .memory.")
@click.option("--no-register", is_flag=True, help="Do not register the MCP server with Claude Code.")
def memory_setup(kind, name, path, source, no_register):
    """Set this project and machine up to use a memory store: find or create the store, point every
    worktree of the clone at it, and register the MCP server with Claude Code at user scope.
    Run it again on another machine to join a project whose store already exists."""
    GimmicksCliApp().memory_setup(kind=kind, name=name, path=path, source=source, register=not no_register)


@memory.command("init")
@click.option("--root", default=".memory", show_default=True, type=click.Path(file_okay=False),
              help="Where to create the store.")
def memory_init(root):
    """Create an empty store with the base vocabulary. `setup` does this for you."""
    GimmicksCliApp().memory_init(root=root)


@memory.command("mcp")
@_ROOT
def memory_mcp(root):
    """Serve the project's store to an agent over MCP (stdio). With no --root it is found from the
    directory the host starts it in, so one registration serves every project and worktree."""
    GimmicksCliApp().memory_mcp(root=root)


@memory.command("accept-pack")
@click.argument("proposal", type=click.Path(exists=True, dir_okay=False))
@click.option("--replace", is_flag=True, help="Replace a pack of the same name that is already live.")
@_ROOT
def memory_accept_pack(proposal, replace, root):
    """Accept a drafted vocabulary pack: check it, make it live, and add its cited documents to the
    inventory. A person runs this after reading the study report — the agent only drafts."""
    GimmicksCliApp().memory_accept_pack(proposal=proposal, replace=replace, root=root)


@memory.command("ingest")
@click.argument("corpus", type=click.Path(exists=True, dir_okay=False))
@_ROOT
def memory_ingest(corpus, root):
    """Ingest a hand-written corpus file, reconciled by slug and digest."""
    GimmicksCliApp().memory_ingest(corpus=corpus, root=root)


@memory.command("accept")
@click.argument("memory_ref", metavar="MEMORY")
@click.option("--reason", default="", help="Why the pattern holds; kept in the audit log.")
@_ROOT
def memory_accept(memory_ref, reason, root):
    """Accept a generalisation: from the next read its instances fold under it.
    A person runs this after reading reviews/session-<n>.md — the agent has no tool for it."""
    GimmicksCliApp().memory_accept(memory=memory_ref, reason=reason, root=root)


@memory.command("status")
@_ROOT
def memory_status(root):
    """Where the store stands: its version, reviews and pending proposals."""
    GimmicksCliApp().memory_status(root=root)


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
