import pytest

from pygim.magic import gim_object
from pygim.magic.gim_object import GimObject



"""
def test_gim_object():
    class Example(GimObject, instance_cached=True):
        pass

    e1 = Example()
    e2 = Example()

    assert id(e1) == id(e2)
"""

def test_gim_object_knows_its_subclasses():
    class ExampleBase(GimObject, track_subclasses=True):
        pass

    class Example1(ExampleBase): pass
    class Example2(ExampleBase): pass
    class Example3(ExampleBase): pass

    assert ExampleBase.subclasses == [Example1, Example2, Example3]



if __name__ == "__main__":
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, gim_object, coverage=False)