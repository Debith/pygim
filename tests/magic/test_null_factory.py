import pytest

#from pygim.magic import null_factory

from pygim.magic import null_object


class NullMeta(type):
    def __call__(self, *args, **kwargs):
        return super().__call__(*args, **kwargs)


class NullBase(metaclass=NullMeta):
    pass


class Example(NullBase):
    def __init__(self, test):
        self._test = test

    def __eq__(self, other):
        return self._test == other._test



def test_equality():
    good1 = Example(42)
    good2 = Example(42)

    assert good1 == good2

    fail1 = Example("Fourty-two")

    assert good1 != fail1

    null1 = Example("this", "that")


if __name__ == "__main__":
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, null_object, coverage=False)