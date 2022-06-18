# -*- coding: utf-8 -*-

import pytest

from pygim.magic.patch import transfer_ownership


class OuterSource:
    def source_func(self):
        pass

class OuterTarget:
    pass


def test_transfer_function_to_class_outer_class_stats():
    transfer_ownership(OuterSource.source_func, OuterTarget)

    assert hasattr(OuterTarget, "source_func")

    left = OuterTarget.source_func
    right = OuterSource.source_func

    actual_qualname = left.__qualname__
    expected_qualname = right.__qualname__.replace('OuterSource', 'OuterTarget')
    assert actual_qualname == expected_qualname
    assert left.__module__ == right.__module__
    assert left.__name__ == right.__name__



def test_transfer_function_to_class_inner_class_stats():
    class InnerSource:
        def source_func(self):
            pass

    class InnerTarget:
        pass

    transfer_ownership(InnerSource.source_func, InnerTarget)

    assert hasattr(InnerTarget, "source_func")

    left = InnerTarget.source_func
    right = InnerSource.source_func

    actual_qualname = left.__qualname__
    expected_qualname = right.__qualname__.replace('InnerSource', 'InnerTarget')
    assert actual_qualname == expected_qualname
    assert left.__module__ == right.__module__
    assert left.__name__ == right.__name__



class OuterSourceWithData:
    def get_public(self):
        return self.public

    def get_protected(self):
        return self._protected

    def get_private(self):
        return self.__private


class OuterTargetWithData:
    def __init__(self):
        self.public = 42
        self._protected = 42
        self.__private = 42


def test_transfer_function_to_class_outer_class_with_data():
    transfer_ownership(OuterSourceWithData.get_public, OuterTargetWithData)
    transfer_ownership(OuterSourceWithData.get_protected, OuterTargetWithData)
    transfer_ownership(OuterSourceWithData.get_private, OuterTargetWithData)

    target = OuterTargetWithData()

    assert target.get_public() == 42
    assert target.get_protected() == 42
    assert target.get_private() == 42


if __name__ == "__main__":
    from pygim.utils.coverage import run_tests
    run_tests(__file__, coverage=False)