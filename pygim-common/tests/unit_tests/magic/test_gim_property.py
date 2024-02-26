import pytest
import time
from io import StringIO
import re

from pygim.performance import quick_timer


def _get_execution_time(func, *args, **kwargs):
    stream = StringIO()
    with quick_timer(printer=stream.write):
        func(*args, **kwargs)
    return float(re.search(r"\d+\.\d+", stream.getvalue()).group(0))


def test_gim_property_can_be_cached():
    from _pygim._magic import gimmick

    class Object(gimmick):
        @gimmick.gim_property(cached=True)
        def prop(self):
            time.sleep(0.2)
            return 42

    o = Object()

    time_elapsed = _get_execution_time(lambda: o.prop)
    assert time_elapsed >= 0.2

    time_elapsed = _get_execution_time(lambda: o.prop)
    assert time_elapsed < 0.2


def test_cached_gim_property_is_not_confused_about_its_owner():
    from _pygim._magic import gimmick

    class Object(gimmick):
        def __init__(self, value):
            self.value = value

        @gimmick.gim_property(cached=True)
        def prop(self):
            return self.value

    o1 = Object(41)
    o2 = Object(42)

    assert o1.prop == 41
    assert o2.prop == 42


def test_cached_gim_property_is_not_confused_about_its_owner_when_carbage_collected():
    from _pygim._magic import gimmick
    from gc import collect

    class Object(gimmick):
        def __init__(self, value):
            self.value = value

        @gimmick.gim_property(cached=True)
        def prop(self):
            return self.value

    previous_ids = set()
    for i in range(100):
        o = Object(i)
        if id(o) in previous_ids:
            assert False, "Object was not garbage collected!"
        previous_ids.add(id(o))
        assert o.prop == i
        del o
        collect()  # force garbage collection


def test_cached_gim_property_supports_slots():
    from _pygim._magic import gimmick

    class Object(gimmick):
        __slots__ = ("value",)

        def __init__(self, value):
            self.value = value

        @gimmick.gim_property(cached=True)
        def prop(self):
            time.sleep(0.2)
            return self.value

    o = Object(42)
    time_elapsed = _get_execution_time(lambda: o.prop)
    assert time_elapsed >= 0.2

    time_elapsed = _get_execution_time(lambda: o.prop)
    assert time_elapsed < 0.2


def test_cached_gim_property_removes_entries_from_cache_when_owner_is_deleted():
    from _pygim._magic import gimmick
    from gc import collect

    class Object(gimmick):
        def __init__(self, value):
            self.value = value

        @gimmick.gim_property(cached=True)
        def prop(self):
            return self.value

    o = Object(42)
    time_elapsed = _get_execution_time(lambda: o.prop)
    assert time_elapsed >= 0.2

    del o
    collect()  # force garbage collection

    time_elapsed = _get_execution_time(lambda: o.prop)
    assert time_elapsed >= 0.2


if __name__ == '__main__':
    pytest.main([__file__, "--capture=no"])