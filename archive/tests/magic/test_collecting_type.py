import pytest

from pygim.magic import collecting_type
CollectingType = collecting_type.CollectingType


def test_collecting_type_knows_its_subclasses():
    class ExampleBase(CollectingType):
        pass

    class Example1(ExampleBase): pass
    class Example2(ExampleBase): pass
    class Example3(ExampleBase): pass

    assert ExampleBase.SUBCLASSES == [Example1, Example2, Example3]


if __name__ == "__main__":
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, collecting_type, coverage=False)
