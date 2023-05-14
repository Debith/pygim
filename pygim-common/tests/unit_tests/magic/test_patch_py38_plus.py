# -*- coding: utf-8 -*-

import pytest

# FUNCTIONS

def func1(pos_arg, /, pos_or_kw_arg, *, kw_arg):
    return pos_arg, pos_or_kw_arg, kw_arg

def func2(pos_arg, /, pos_or_kw_arg, *, kw_arg=20):
    return pos_arg, pos_or_kw_arg, kw_arg

def func3(pos_arg, /, pos_or_kw_arg=10, *, kw_arg):
    return pos_arg, pos_or_kw_arg, kw_arg

def func4(pos_arg, /, pos_or_kw_arg=10, *, kw_arg=20):
    return pos_arg, pos_or_kw_arg, kw_arg

def func5(pos_arg, /, pos_or_kw_arg, *args, kw_arg, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

def func6(pos_arg, /, pos_or_kw_arg, *args, kw_arg=20, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

def func7(pos_arg, /, pos_or_kw_arg=10, *args, kw_arg, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

def func8(pos_arg, /, pos_or_kw_arg=10, *args, kw_arg=20, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs


# METHOD-LIKE

def meth1(self, pos_arg, /, pos_or_kw_arg, *, kw_arg):
    return pos_arg, pos_or_kw_arg, kw_arg

def meth2(self, pos_arg, /, pos_or_kw_arg, *, kw_arg=20):
    return pos_arg, pos_or_kw_arg, kw_arg

def meth3(self, pos_arg, /, pos_or_kw_arg=10, *, kw_arg):
    return pos_arg, pos_or_kw_arg, kw_arg

def meth4(self, pos_arg, /, pos_or_kw_arg=10, *, kw_arg=20):
    return pos_arg, pos_or_kw_arg, kw_arg

def meth5(self, pos_arg, /, pos_or_kw_arg, *args, kw_arg, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

def meth6(self, pos_arg, /, pos_or_kw_arg, *args, kw_arg=20, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

def meth7(self, pos_arg, /, pos_or_kw_arg=10, *args, kw_arg, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

def meth8(self, pos_arg, /, pos_or_kw_arg=10, *args, kw_arg=20, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs


class Methods:
    def func1(self, pos_arg, /, pos_or_kw_arg, *, kw_arg):
        return pos_arg, pos_or_kw_arg, kw_arg

    def func2(self, pos_arg, /, pos_or_kw_arg, *, kw_arg=20):
        return pos_arg, pos_or_kw_arg, kw_arg

    def func3(self, pos_arg, /, pos_or_kw_arg=10, *, kw_arg):
        return pos_arg, pos_or_kw_arg, kw_arg

    def func4(self, pos_arg, /, pos_or_kw_arg=10, *, kw_arg=20):
        return pos_arg, pos_or_kw_arg, kw_arg

    def func5(self, pos_arg, /, pos_or_kw_arg, *args, kw_arg, **kwargs):
        return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

    def func6(self, pos_arg, /, pos_or_kw_arg, *args, kw_arg=20, **kwargs):
        return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

    def func7(self, pos_arg, /, pos_or_kw_arg=10, *args, kw_arg, **kwargs):
        return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

    def func8(self, pos_arg, /, pos_or_kw_arg=10, *args, kw_arg=20, **kwargs):
        return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs


@pytest.mark.parametrize("ofunc, args, kwargs", [
    (func1, (1, 2), dict(kw_arg=3)),
    (func1, (1,), dict(pos_or_kw_arg=2, kw_arg=3)),
    (func2, (1, 2), {}),
    (func2, (1,), dict(pos_or_kw_arg=2)),
    (func3, (1,), dict(kw_arg=3)),
    (func4, (1,), {}),
    (func5, (1, 2), dict(kw_arg=3)),
    (func6, (1, 2), {}),
    (func7, (1,), dict(kw_arg=3)),
    (func8, (1,), {}),
])
def test_mutable_function_correctly_duplicates_the_function(ofunc, args, kwargs):
    from _pygim._magic._patch import MutableFuncObject

    nfunc = MutableFuncObject(ofunc).freeze()
    assert list(dir(ofunc)) == list(dir(nfunc))

    expected_result = ofunc(*args, **kwargs)

    try:
        actual_result = nfunc(*args, **kwargs)
    except TypeError as e:
        assert False, e

    if actual_result != expected_result:
        assert False, f"{actual_result} != {expected_result}"


@pytest.mark.parametrize("oclass, omethod_name, args, kwargs", [
    (Methods, "func1", (1, 2), dict(kw_arg=3)),
    (Methods, "func1", (1,), dict(pos_or_kw_arg=2, kw_arg=3)),
    (Methods, "func2", (1, 2), {}),
    (Methods, "func2", (1,), dict(pos_or_kw_arg=2)),
    (Methods, "func3", (1,), dict(kw_arg=3)),
    (Methods, "func4", (1,), {}),
    (Methods, "func5", (1, 2), dict(kw_arg=3)),
    (Methods, "func6", (1, 2), {}),
    (Methods, "func7", (1,), dict(kw_arg=3)),
    (Methods, "func8", (1,), {}),
])
def test_mutable_function_correctly_duplicates_the_method(oclass, omethod_name, args, kwargs):
    from _pygim._magic._patch import MutableFuncObject

    ofunc = getattr(oclass, omethod_name)

    nfunc = MutableFuncObject(ofunc).freeze()
    _odir = list(dir(ofunc))
    _ndir = list(dir(nfunc))
    if _odir != _ndir:
        assert False, f"{_odir} != {_ndir}"

    oobj = oclass()
    omethod = getattr(oobj, omethod_name)
    expected_result = omethod(*args, **kwargs)

    try:
        actual_result = nfunc(oobj, *args, **kwargs)
    except TypeError as e:
        assert False, e

    if actual_result != expected_result:
        assert False, f"{actual_result} != {expected_result}"


@pytest.mark.parametrize("ofunc, args, kwargs", [
    (meth1, (1, 2), dict(kw_arg=3)),
    (meth1, (1,), dict(pos_or_kw_arg=2, kw_arg=3)),
    (meth2, (1, 2), {}),
    (meth2, (1,), dict(pos_or_kw_arg=2)),
    (meth3, (1,), dict(kw_arg=3)),
    (meth4, (1,), {}),
    (meth5, (1, 2), dict(kw_arg=3)),
    (meth6, (1, 2), {}),
    (meth7, (1,), dict(kw_arg=3)),
    (meth8, (1,), {}),
])
def test_function_assigned_to_class_properly(ofunc, args, kwargs):
    class NewOwner:
        def __init__(self):
            self.public = 1
            self._protected = 2
            self.__private = 3

        def original(self):
            return self.public, self._protected, self.__private

    from _pygim._magic._patch import MutableFuncObject
    nfunc = MutableFuncObject(ofunc).assign_to_class(NewOwner).freeze()

    expected_result = ofunc(NewOwner(), *args, **kwargs)

    try:
        actual_result = nfunc(NewOwner(), *args, **kwargs)
    except TypeError as e:
        assert False, e

    if actual_result != expected_result:
        assert False, f"{actual_result} != {expected_result}"


if __name__ == '__main__':
    pytest.main([__file__])
