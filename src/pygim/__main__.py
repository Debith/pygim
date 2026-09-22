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
    if ctx.invoked_subcommand is None and ctx.meta.get("free_text") is None:
        click.echo(ctx.get_help())


@cli_oo.group()
def enact():
    """ENACT: knowledge found by the kind of problem being solved, and what is learned from using it."""


_ROOT = click.option("--root", default=None, type=click.Path(file_okay=False),
                     help="The ENACT store. Default: $PYGIM_ENACT_ROOT, then `git config pygim.enact` "
                          "(shared by every worktree), then a .enact above the working directory.")


@enact.command("setup")
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
@click.pass_obj
def enact_setup(where, kind, name, path, source, no_register):
    """Set this project and machine up to use a memory store: find or create the store, point every
    worktree of the clone at it, and register the MCP server with Claude Code at user scope.
    Run it again on another machine to join a project whose store already exists."""
    GimmicksCliApp().enact_setup(where=where, kind=kind, name=name, path=path, source=source, register=not no_register)


@enact.command("call")
@click.argument("name")
@click.option("--json", "arguments", default=None,
              help="The tool's arguments as a JSON object. Without it, they are read from stdin.")
@click.option("--session", type=int, default=None,
              help="Write as this session, so several calls belong together and `review` can gather "
                   "them. Default: each call is a session of its own.")
@_ROOT
@click.pass_obj
def enact_call(where, name, arguments, session, root):
    """Call one tool of the agent surface from a shell, and print its JSON result.

    The same dispatch the MCP server uses, so a script drives the whole stack through commands:

        oo enact call read --json '{"hard": ["domain=pygim", "artifact=store", "task=implement"]}'

    Each call is a process, and a process is a session. To make several calls one session:

        export PYGIM_ENACT_SESSION=$(oo enact call session --json '{}' | jq -r .session)

    A refusal is a result: it prints with `refused` and exits 0. Exit 1 means the call could not be
    made at all — no such tool, or the arguments were not JSON.
    """
    GimmicksCliApp().enact_call(where=where.with_root(root).with_session(session), name=name, arguments=arguments)


@enact.command("mcp")
@_ROOT
@click.pass_obj
def enact_mcp(where, root):
    """Serve the project's store to an agent over MCP (stdio). With no --root it is found from the
    directory the host starts it in, so one registration serves every project and worktree."""
    GimmicksCliApp().enact_mcp(where=where.with_root(root))


@enact.command("stores")
@flag_opt("--remote", "remote", help="Also list the stores kept in the remote that are not checked out here.")
@_ROOT
@click.pass_obj
def enact_stores(where, remote, root):
    """List the stores this machine holds, as a session can name them with `scope`. Nothing is
    configured: a store is found by its policy's name or its directory's."""
    GimmicksCliApp().enact_stores(where=where.with_root(root), remote=remote)


@enact.command("mailbox")
@click.option("--post", "text", default=None, help="Leave a message instead of listing.")
@click.option("--kind", type=click.Choice(["feedback", "request", "comment"]), default="comment",
              help="--post: what kind of message it is.")
@click.option("--to", "to", default=None, help="--post: who it is for (default: whoever reads next).")
@click.option("--about", default=None, help="--post: what it concerns — a memory, a path, a report.")
@click.option("--resolves", default=None, help="--post: the message id this closes.")
@flag_opt("--all", "show_all", help="List resolved messages too.")
@_ROOT
@click.pass_obj
def enact_mailbox(where, text, kind, to, about, resolves, show_all, root):
    """Messages other sessions, agents and people left in this store — feedback, requests and
    comments. Lists what is open; `--post` leaves one."""
    GimmicksCliApp().enact_mailbox(where=where.with_root(root), text=text, kind=kind, to=to, about=about, resolves=resolves,
                                    show_all=show_all)


@enact.command("reload")
@flag_opt("--signal", "signal_servers",
          help="Also SIGHUP every running server, including other projects'. A server older than "
               "this feature has no handler for SIGHUP and will exit instead of reloading.")
@click.pass_obj
def enact_reload(where, signal_servers):
    """Ask the MCP servers on this project's store, and on the global one, to restart into the
    installed code — after upgrading pygim, or editing the server. Each reloads between messages,
    so the host's connection survives."""
    GimmicksCliApp().enact_reload(where=where, signal_servers=signal_servers)


@enact.command("ingest")
@click.argument("corpus", type=click.Path(exists=True, dir_okay=False))
@_ROOT
@click.pass_obj
def enact_ingest(where, corpus, root):
    """Ingest a hand-written corpus file, reconciled by slug and digest."""
    GimmicksCliApp().enact_ingest(where=where.with_root(root), corpus=corpus)


@enact.command("accept")
@click.argument("memory_ref", metavar="[MEMORY]", required=False)
@click.option("--pack", "pack", default=None, type=click.Path(exists=True, dir_okay=False),
              help="Accept a drafted vocabulary pack instead: check it, make it live, add its cited documents.")
@click.option("--reason", default="", help="MEMORY: why the pattern holds; kept in the audit log.")
@click.option("--replace", is_flag=True, help="--pack: replace a pack of the same name that is already live.")
@flag_opt("--all", "walk", help="Read every generalisation waiting for you, one at a time, and answer each.")
@flag_opt("-y", "--yes", "assume_yes", help="Accept without showing it first. For scripts; a person should read it.")
@_ROOT
@click.pass_obj
def enact_accept(where, memory_ref, pack, reason, replace, walk, assume_yes, root):
    """Accept what the agent drafted, after reading it: a generalisation, whose instances then fold
    under it; or a vocabulary pack (--pack), which then becomes the vocabulary. With no argument it
    shows what is waiting, in words rather than keys. A person runs this — the agent has no tool for it."""
    GimmicksCliApp().enact_accept(where=where.with_root(root), memory=memory_ref, pack=pack, reason=reason, replace=replace,
                                   walk=walk, assume_yes=assume_yes)


@enact.command("status")
@flag_opt("--standing", "standing",
          help="Print the standing knowledge instead — every preference in full, as a session receives "
               "it — and nothing else, so a host's session-start hook can put it in front of an agent.")
@_ROOT
@click.pass_obj
def enact_status(where, standing, root):
    """Where the store stands: its version, reviews and pending proposals."""
    GimmicksCliApp().enact_status(where=where.with_root(root), standing=standing)


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
