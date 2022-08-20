# -*- coding: utf-8 -*-
""" Tests Each trait. """

import pytest

from pygim.kernel.magic.each import Each, for_each


def test_dir_shows_available_functions():
    class Single:
        def first(self): return 1
        def second(self): return 2
        def third(self): return 3

    class Container:
        def __init__(self, items):
            self._items = items

        def __iter__(self):
            yield from self._items

        each = Each(callable=True)

    collection = Container([Single()])
    assert dir(collection.each) == ['first', 'second', 'third']

    collection = Container([])
    assert dir(collection.each) == []


def test_call_each_function():
    class Single:
        def first(self): return 1
        def second(self): return 2
        def third(self): return 3

    class Container:
        def __init__(self, items):
            self._items = items

        def __iter__(self):
            yield from self._items

        def extend(self, items):
            self._items.extend(items)

        each = Each(callable=True)

    collection = Container([Single(), Single(), Single()])
    r = collection.each.first()

    assert r == [1, 1, 1]

    collection.extend([Single(), Single(), Single()])

    r = collection.each.first()
    assert r == [1, 1, 1, 1, 1, 1]


def test_call_each_function_with_args_and_kwargs():
    class Single:
        def first(self, arg, *, kwarg): return arg, kwarg

    class Container:
        def __init__(self, items):
            self._items = items

        def __iter__(self):
            yield from self._items

        def extend(self, items):
            self._items.extend(items)

        each = Each(callable=True)

    collection = Container([Single(), Single(), Single()])
    r = collection.each.first(1, kwarg="two")

    assert r == [(1, 'two'), (1, 'two'), (1, 'two')]


def test_call_each_object_in_list():
    class Single:
        def first(self, arg, *, kwarg): return arg, kwarg

    collection = [Single(), Single(), Single()]

    r = for_each(collection).first(1, kwarg="two")

    assert r == [(1, 'two'), (1, 'two'), (1, 'two')]


if __name__ == "__main__":
    pytest.main([__file__])