
import pytest

from pygim.system.primitives.string_list import StringList, StringListMeta


NewLines = StringListMeta('NewLines')

def test_string_list():
    key = NewLines('test')
    key += "next"
    assert str(key) == 'test\nnext'

    key = NewLines('test')
    key2 = NewLines('next')
    key3 = key + key2
    assert key3 == NewLines('test\nnext')
    assert str(key3) == 'test\nnext'
    assert str(key) == 'test', "key should not have changed."
    assert str(key2) == 'next', "key should not have changed."

    key1 = NewLines("first\nsecond")
    key2 = NewLines("third\nfourth")
    comb = key1 + key2

    assert str(comb) == "first\nsecond\nthird\nfourth"

    key1 = NewLines(["first", "second"])
    key2 = NewLines(["third", "fourth"])
    comb = key1 + key2

    assert str(comb) == "first\nsecond\nthird\nfourth"


class Arrows(StringList):
    _sep = ' -> '


def test_arrow_separated_list():
    left = Arrows('source')
    right = Arrows('dest')
    comb = left + right

    assert str(comb) == "source -> dest"


class Lines(StringList, sep='=='):
    """"""

def test_line_separated_list():
    left = Arrows('source')
    right = Arrows('dest')
    comb = left + right

    assert str(comb) == "source == dest"



if __name__ == "__main__":
    pytest.main([__file__])