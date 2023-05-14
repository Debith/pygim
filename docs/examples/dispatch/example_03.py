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


class ClassDispatchExample:
    @dispatch
    def dispatch_by_type(self):
        raise NotImplementedError()

    @dispatch_by_type.register("first")
    def _(self, arg):
        return f"First got {arg} in method"

    @dispatch_by_type.register("second")
    def _(self, arg):
        return f"Second got {arg} in method"


example = ClassDispatchExample()
assert example.dispatch_by_type("first") == "First got first in method"
assert example.dispatch_by_type("second") == "Second got second in method"