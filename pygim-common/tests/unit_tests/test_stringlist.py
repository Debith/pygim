
import pytest

from pygim.kernel.string_list import StringList, StringListMeta


NewLines = StringListMeta('NewLines')


@pytest.mark.parametrize('key1, key2, expected_result', [
    ('test', 'str_type', 'test\nstr_type'),
    ('test', b'bytes_type', 'test\nbytes_type'),
    ('test', ['next', 'list_type'], 'test\nnext\nlist_type'),
    ('test', StringList('next', 'string_list_type'), 'test\nnext\nstring_list_type'),
    ('test', ['next', b'next2', ['next3', 'next4']], 'test\nnext\nnext2\nnext3\nnext4'),
])
def test_string_list(key1, key2, expected_result):
    key1 = NewLines(key1)
    try:
        actual_result = key1 + key2
    except Exception:
        key1 + key2
        pytest.fail(f"Cannot add {key2} to {key1.__class__.__name__}")
    actual_result = str(actual_result)

    if actual_result != expected_result:
        pytest.fail(f"expected ``{expected_result}`` but got ``{actual_result}``")

"""

class Arrows(StringList):
    _sep = ' -> '


def test_arrow_separated_list():
    left = Arrows('source')
    right = Arrows('dest')
    comb = left + right

    assert str(comb) == "source -> dest"


class Lines(StringList, sep='=='):
    """ """


def test_line_separated_list():
    left = Arrows('source')
    right = Arrows('dest')
    comb = left + right

    assert str(comb) == "source == dest"

"""

if __name__ == "__main__":
    pytest.main([__file__])