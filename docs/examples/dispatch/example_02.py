# type: ignore
from pygim import dispatch


@dispatch
def dispatch_by_type():
    raise NotImplementedError()


@dispatch_by_type.register(int, int)
def _(number1, number2):
    return f"Got {type(number1).__name__} and {type(number2).__name__}"


@dispatch_by_type.register(int, float)
def _(number1, number2):
    return f"Got {type(number1).__name__} and {type(number2).__name__}"


assert dispatch_by_type(122, 123) == "Got int and int"
assert dispatch_by_type(122, 123.0) == "Got int and float"


class ClassDispatchExample:
    @dispatch
    def dispatch_by_type(self):
        raise NotImplementedError()

    @dispatch_by_type.register(int, int)
    def _(self, number1, number2):
        return f"Got {type(number1).__name__} and {type(number2).__name__} in method"

    @dispatch_by_type.register(int, float)
    def _(self, number1, number2):
        return f"Got {type(number1).__name__} and {type(number2).__name__} in method"


example = ClassDispatchExample()
assert example.dispatch_by_type(122, 123) == "Got int and int in method"
assert example.dispatch_by_type(122, 123.0) == "Got int and float in method"