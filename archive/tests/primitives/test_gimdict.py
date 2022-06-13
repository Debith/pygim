
import pytest

from pygim.system.primitives.gimdict import GimDict, GimDictKey

def test_gimdict_subset():
    items = GimDict(first=1, second=2, third=3)
    filtered1 = items & ('first', )
    filtered2 = items & ('first', 'first')
    filtered3 = items & {'second'}
    filtered4 = items & {'second', 'second'}

    assert filtered1 == GimDict(first=1)
    assert filtered2 == GimDict(first=1)
    assert filtered3 == GimDict(second=2)
    assert filtered4 == GimDict(second=2)



def test_left_shift():
    pass


if __name__ == "__main__":
    pytest.main([__file__])