from pygim.typing import AnyKwargs
from pygim._cli import flag_opt as flag_opt
from pygim._cli.cliapp import GimmicksCliApp as GimmicksCliApp

def cli() -> None: ...
def clean_up(**kwargs: AnyKwargs) -> None: ...