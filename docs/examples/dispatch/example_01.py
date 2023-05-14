# type: ignore
from pygim import dispatch


@dispatch
def dispatch_by_type():
    raise NotImplementedError()


@dispatch_by_type.register(int)
def _(value):
    return f"This is {type(value).__name__}"


@dispatch_by_type.register(float)
def _(value):
    return f"This is {type(value).__name__}"


assert dispatch_by_type(123) == "This is int"
assert dispatch_by_type(123.0) == "This is float"


class ClassDispatchExample:
    @dispatch
    def dispatch_by_type(self):
        raise NotImplementedError()

    @dispatch_by_type.register(int)
    def _(self, value):
        return f"This is {type(value).__name__}"

    @dispatch_by_type.register(float)
    def _(self, value):
        return f"This is {type(value).__name__}"


example = ClassDispatchExample()
assert example.dispatch_by_type(123) == "This is int"
assert example.dispatch_by_type(123.0) == "This is float"