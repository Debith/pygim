import pytest

from pygim.magic import gim_object
from pygim.magic.gim_object import GimObject



def test_gim_object():
    class Example(GimObject, instance_cached=True):
        pass

    e1 = Example()
    e2 = Example()

    assert id(e1) == id(e2)





if __name__ == "__main__":
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, gim_object, coverage=False)