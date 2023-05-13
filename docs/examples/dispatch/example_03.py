# type: ignore
from pygim import dispatch


@dispatch
def dispatch_by_type():
    raise NotImplementedError()


@dispatch_by_type.register("first")
def _(arg):
    return f"First got {arg}"


@dispatch_by_type.register("second")
def _(arg):
    return f"Second got {arg}"


assert dispatch_by_type("first") == "First got first"
assert dispatch_by_type("second") == "Second got second"
