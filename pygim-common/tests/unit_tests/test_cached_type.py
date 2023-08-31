# -*- coding: utf-8 -*-


def test_class_is_singleton():
    from _pygim._magic._cached_type import create_cached_class

    Empty = create_cached_class("Empty", cache_class=True, cache_instance=False)
    _Empty = create_cached_class("Empty", cache_class=True, cache_instance=False)

    assert id(Empty) == id(_Empty)


def test_instance_is_singleton():
    from _pygim._magic._cached_type import create_cached_class

    Empty = create_cached_class("Empty", cache_class=True, cache_instance=True)
    _Empty = create_cached_class("Empty", cache_class=True, cache_instance=True)

    empty1 = Empty()
    empty2 = Empty()
    empty3 = _Empty()
    empty4 = _Empty()

    assert id(empty1) == id(empty2) == id(empty3) == id(empty4)


def test_no_caching():
    from _pygim._magic._cached_type import create_cached_class

    Empty1 = create_cached_class("Empty1", cache_class=False, cache_instance=False)
    Empty2 = create_cached_class("Empty2", cache_class=False, cache_instance=False)

    empty11 = Empty1()
    empty12 = Empty1()
    empty21 = Empty2()
    empty22 = Empty2()

    assert len(set([id(empty11), id(empty12), id(empty21), id(empty22)])) == 4


def test_only_instance_caching():
    from _pygim._magic._cached_type import create_cached_class

    Empty1 = create_cached_class("Empty1", cache_class=False, cache_instance=True)
    Empty2 = create_cached_class("Empty2", cache_class=False, cache_instance=True)

    empty11 = Empty1()
    empty12 = Empty1()
    empty21 = Empty2()
    empty22 = Empty2()

    assert id(empty11) == id(empty12)
    assert id(empty21) == id(empty22)
    assert id(empty11) != id(empty21)
    assert id(empty12) != id(empty22)


def test_class_is_singleton_via_cached_type():
    from _pygim._magic._cached_type import CachedType
    class Empty(CachedType):
        """ A More traditional way to create cached type. """

    assert id(Empty) == id(Empty)


if __name__ == "__main__":
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, "_pygim._magic._cached_type", coverage=False)
