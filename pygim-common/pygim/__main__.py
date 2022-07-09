"""
Python Gimmicks Command-Line Interface.
"""

import sys
import pathlib
import shutil
import click

def _echo(msg, quiet):
    if not quiet:
        click.echo(msg)


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
@click.option("-d", "--dry-run", is_flag=True, default=False,
              help="Just print items to be deleted.")
@click.option("-q", "--quiet", is_flag=True, default=False,
              help="Sssh! No output!")
def remove_caches(dry_run, quiet):
    """ Removes __cache__ folders in current workdir. """
    if dry_run and quiet:
        sys.exit("Can't be quiet and do dry run at the same time! Well, can... but it doesn't make sense.")

    _echo(f"Starting clean up in `{pathlib.Path.cwd()}`", quiet)
    del_msg = "Deleting" if not dry_run else "Would delete"

    for fname in pathlib.Path.cwd().rglob("__pycache__"):
        _echo(f"{del_msg} folder with its contents: {fname}", quiet)

        if not dry_run:
            shutil.rmtree(fname)

    _echo("Clean up complete!", quiet)
