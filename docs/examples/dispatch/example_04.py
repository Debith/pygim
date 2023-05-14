# type: ignore
import enum
from pygim import dispatch

class ExampleEnum(enum.Enum):
    FIRST = "First Entry"
    SECOND = "Second Entry"


@dispatch
def dispatch_by_type():
    raise NotImplementedError()


@dispatch_by_type.register(ExampleEnum.FIRST)
def _(__enum, __number_arg):
    return f"First got `{__enum.value}` and {__number_arg}"


@dispatch_by_type.register(ExampleEnum.SECOND)
def _(__enum, __number_arg):
    return f"Second got `{__enum.value}` and {__number_arg}"


assert dispatch_by_type(ExampleEnum.FIRST, 1) == "First got `First Entry` and 1"
assert dispatch_by_type(ExampleEnum.SECOND, 2) == "Second got `Second Entry` and 2"


class ClassDispatchExample:
    @dispatch
    def dispatch_by_type():
        raise NotImplementedError()

    @dispatch_by_type.register(ExampleEnum.FIRST)
    def _(__enum, __number_arg):
        return f"First got `{__enum.value}` and {__number_arg} in method"

    @dispatch_by_type.register(ExampleEnum.SECOND)
    def _(__enum, __number_arg):
        return f"Second got `{__enum.value}` and {__number_arg} in method"


example = ClassDispatchExample()
assert example.dispatch_by_type(ExampleEnum.FIRST, 1) == "First got `First Entry` and 1 in method"
assert example.dispatch_by_type(ExampleEnum.SECOND, 2) == "Second got `Second Entry` and 2 in method"