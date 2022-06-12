"""
Command-Line Interface for Pygim
"""
import click


@click.group()
def cli():
    """ Pygim group """


@cli.command()
def test():
    pass




if __name__ == "__main__":
    cli()