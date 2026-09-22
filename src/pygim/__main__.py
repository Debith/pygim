# -*- coding: utf-8 -*-
"""
Python Gimmicks Command-Line Interface.
"""

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


@click.group(cls=_OoGroup, invoke_without_command=True, tagline="AI powered Python Gimmicks")
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
@click.option("--local", "kind", flag_value="local",
              help="Keep the store in the project as .memory, committed with the code on this branch.")
@click.option("--global", "kind", flag_value="global",
              help="Create this machine's global store, for knowledge about no single project "
                   "(domain=any): every project's sessions read it, and each write is committed at once.")
@click.option("--name", default=None, help="--user: the store's name (default: the project directory's name).")
@click.option("--path", "path", default=None, type=click.Path(file_okay=False),
              help="--branch: where to check the branch out (default: beside the main worktree). "
                   "--global: where the global store lives (default: your user data directory).")
@click.option("--from", "source", default=None, type=click.Path(exists=True, file_okay=False),
              help="Start the new store as a copy of an existing one, such as a project's .memory.")
@click.option("--no-register", is_flag=True, help="Do not register the MCP server with Claude Code.")
def memory_setup(kind, name, path, source, no_register):
    """Set this project and machine up to use a memory store: find or create the store, point every
    worktree of the clone at it, and register the MCP server with Claude Code at user scope.
    Run it again on another machine to join a project whose store already exists."""
    GimmicksCliApp().memory_setup(kind=kind, name=name, path=path, source=source, register=not no_register)


@memory.command("mcp")
@_ROOT
def memory_mcp(root):
    """Serve the project's store to an agent over MCP (stdio). With no --root it is found from the
    directory the host starts it in, so one registration serves every project and worktree."""
    GimmicksCliApp().memory_mcp(root=root)


@memory.command("stores")
@flag_opt("--remote", "remote", help="Also list the stores kept in the remote that are not checked out here.")
@_ROOT
def memory_stores(remote, root):
    """List the stores this machine holds, as a session can name them with `scope`. Nothing is
    configured: a store is found by its policy's name or its directory's."""
    GimmicksCliApp().memory_stores(remote=remote, root=root)


@memory.command("mailbox")
@click.option("--post", "text", default=None, help="Leave a message instead of listing.")
@click.option("--kind", type=click.Choice(["feedback", "request", "comment"]), default="comment",
              help="--post: what kind of message it is.")
@click.option("--to", "to", default=None, help="--post: who it is for (default: whoever reads next).")
@click.option("--about", default=None, help="--post: what it concerns — a memory, a path, a report.")
@click.option("--resolves", default=None, help="--post: the message id this closes.")
@flag_opt("--all", "show_all", help="List resolved messages too.")
@_ROOT
def memory_mailbox(text, kind, to, about, resolves, show_all, root):
    """Messages other sessions, agents and people left in this store — feedback, requests and
    comments. Lists what is open; `--post` leaves one."""
    GimmicksCliApp().memory_mailbox(text=text, kind=kind, to=to, about=about, resolves=resolves,
                                    show_all=show_all, root=root)


@memory.command("reload")
@flag_opt("--signal", "signal_servers",
          help="Also SIGHUP every running server, including other projects'. A server older than "
               "this feature has no handler for SIGHUP and will exit instead of reloading.")
def memory_reload(signal_servers):
    """Ask the MCP servers on this project's store, and on the global one, to restart into the
    installed code — after upgrading pygim, or editing the server. Each reloads between messages,
    so the host's connection survives."""
    GimmicksCliApp().memory_reload(signal_servers=signal_servers)


@memory.command("ingest")
@click.argument("corpus", type=click.Path(exists=True, dir_okay=False))
@_ROOT
def memory_ingest(corpus, root):
    """Ingest a hand-written corpus file, reconciled by slug and digest."""
    GimmicksCliApp().memory_ingest(corpus=corpus, root=root)


@memory.command("accept")
@click.argument("memory_ref", metavar="[MEMORY]", required=False)
@click.option("--pack", "pack", default=None, type=click.Path(exists=True, dir_okay=False),
              help="Accept a drafted vocabulary pack instead: check it, make it live, add its cited documents.")
@click.option("--reason", default="", help="MEMORY: why the pattern holds; kept in the audit log.")
@click.option("--replace", is_flag=True, help="--pack: replace a pack of the same name that is already live.")
@flag_opt("--all", "walk", help="Read every generalisation waiting for you, one at a time, and answer each.")
@flag_opt("-y", "--yes", "assume_yes", help="Accept without showing it first. For scripts; a person should read it.")
@_ROOT
def memory_accept(memory_ref, pack, reason, replace, walk, assume_yes, root):
    """Accept what the agent drafted, after reading it: a generalisation, whose instances then fold
    under it; or a vocabulary pack (--pack), which then becomes the vocabulary. With no argument it
    shows what is waiting, in words rather than keys. A person runs this — the agent has no tool for it."""
    GimmicksCliApp().memory_accept(memory=memory_ref, pack=pack, reason=reason, replace=replace,
                                   walk=walk, assume_yes=assume_yes, root=root)


@memory.command("status")
@flag_opt("--standing", "standing",
          help="Print the standing knowledge instead — every preference in full, as a session receives "
               "it — and nothing else, so a host's session-start hook can put it in front of an agent.")
@_ROOT
def memory_status(standing, root):
    """Where the store stands: its version, reviews and pending proposals."""
    GimmicksCliApp().memory_status(root=root, standing=standing)


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
