# -*- coding: utf-8 -*-
import tabulate

import pytest


def test_mutable_code_object_returns_identical_code_object_for_function():
    from _pygim._magic._patch import MutableCodeObject

    def my_func(test: int):
        return test * 2

    mutable = MutableCodeObject(my_func.__code__)
    new_codeobj = mutable.freeze()

    assert id(my_func.__code__) != new_codeobj
    assert MutableCodeObject(my_func.__code__) == MutableCodeObject(new_codeobj)
    assert my_func.__code__ == new_codeobj


def test_mutable_code_object_can_modify_owner_of_class():
    from _pygim._magic._patch import MutableCodeObject
    from _pygim._utils._inspect import diff

    class Test:
        def __init__(self):
            self.public = 1
            self._protected = 2
            self.__private = 3

        def my_func(self):
            return self.public, self._protected, self.__private

    mcode_obj = MutableCodeObject(Test.my_func.__code__)
    mcode_obj.rename_owner("New")
    new_mcodeobj = mcode_obj.freeze()

    assert id(Test.my_func.__code__) != new_mcodeobj
    assert Test.my_func.__code__ != new_mcodeobj

    expected = MutableCodeObject(Test.my_func.__code__)
    expected['co_names'] = ('public', '_protected', '_New__private')
    actual = MutableCodeObject(new_mcodeobj)

    assert actual == expected, diff(actual, expected)


def test_new_transferred_method_behaves_as_a_member_of_new_class():
    from _pygim._magic._patch import MutableFuncObject

    class Test:
        def my_func(self):
            return self.public, self._protected, self.__private

    class NewOwner:
        def __init__(self):
            self.public = 1
            self._protected = 2
            self.__private = 3

        def original(self):
            return self.public, self._protected, self.__private

    trait_func = MutableFuncObject(Test.my_func)
    trait_func >> NewOwner

    instance = NewOwner()
    assert instance.my_func() == instance.original()
    assert instance.my_func.__self__ == instance.original.__self__


def test_new_transferred_function_behaves_as_a_member_of_new_class():
    from _pygim._magic._patch import MutableFuncObject

    def my_func(self):
        return self.public, self._protected, self.__private

    class NewOwner:
        def __init__(self):
            self.public = 1
            self._protected = 2
            self.__private = 3

        def original(self):
            return self.public, self._protected, self.__private

    trait_func = MutableFuncObject(my_func)
    trait_func >> NewOwner

    instance = NewOwner()
    assert instance.my_func() == instance.original()
    assert instance.my_func.__self__ == instance.original.__self__
    assert instance.my_func.__qualname__.split(".")[:-1] == instance.original.__qualname__.split(".")[:-1]


def test_move_multiple_functions_at_once():
    from _pygim._magic._traits import transfer_ownership

    class Test1:
        def my_func_1(self):
            return self.public, self._protected, self.__private

        def my_func_2(self):
            return self.public, self._protected, self.__private


    class Test2:
        def my_func_3(self):
            return self.public, self._protected, self.__private

        def my_func_4(self):
            return self.public, self._protected, self.__private


    class NewOwner:
        def __init__(self):
            self.public = 1
            self._protected = 2
            self.__private = 3

        def original(self):
            return self.public, self._protected, self.__private

    transfer_ownership(NewOwner,
                        Test1.my_func_1, Test1.my_func_2,
                        Test2.my_func_3, Test2.my_func_4,
                        )

    instance = NewOwner()
    assert instance.my_func_1() == instance.original()
    assert instance.my_func_2() == instance.original()
    assert instance.my_func_3() == instance.original()
    assert instance.my_func_4() == instance.original()
    assert instance.my_func_1.__self__ == instance.original.__self__
    assert instance.my_func_2.__self__ == instance.original.__self__
    assert instance.my_func_3.__self__ == instance.original.__self__
    assert instance.my_func_4.__self__ == instance.original.__self__


def _module_level_func(self):
    return self.public, self._protected, self.__private


def test_new_transferred_module_function_behaves_as_a_member_of_new_class():
    from _pygim._magic._patch import MutableFuncObject

    class NewOwner:
        def __init__(self):
            self.public = 1
            self._protected = 2
            self.__private = 3

        def original(self):
            return self.public, self._protected, self.__private

    trait_func = MutableFuncObject(_module_level_func)
    trait_func >> NewOwner

    instance = NewOwner()
    assert instance._module_level_func() == instance.original()
    assert instance._module_level_func.__self__ == instance.original.__self__


# FUNCTIONS

def func1(pos_or_kw_arg, *, kw_arg):
    return pos_or_kw_arg, kw_arg

def func2(pos_or_kw_arg, *, kw_arg=20):
    return pos_or_kw_arg, kw_arg

def func3(pos_or_kw_arg=10, *, kw_arg):
    return pos_or_kw_arg, kw_arg

def func4(pos_or_kw_arg=10, *, kw_arg=20):
    return pos_or_kw_arg, kw_arg

def func5(pos_arg, /, pos_or_kw_arg, *, kw_arg):
    return pos_arg, pos_or_kw_arg, kw_arg

def func6(pos_arg, /, pos_or_kw_arg, *, kw_arg=20):
    return pos_arg, pos_or_kw_arg, kw_arg

def func7(pos_arg, /, pos_or_kw_arg=10, *, kw_arg):
    return pos_arg, pos_or_kw_arg, kw_arg

def func8(pos_arg, /, pos_or_kw_arg=10, *, kw_arg=20):
    return pos_arg, pos_or_kw_arg, kw_arg

def func9(pos_arg, *args, kw_arg, **kwargs):
    return pos_arg, args, kw_arg, kwargs

def func10(pos_arg, *args, kw_arg=20, **kwargs):
    return pos_arg, args, kw_arg, kwargs

def func11(pos_arg=10, *args, kw_arg, **kwargs):
    return pos_arg, args, kw_arg, kwargs

def func12(pos_arg=10, *args, kw_arg=20, **kwargs):
    return pos_arg, args, kw_arg, kwargs

def func13(pos_arg, /, pos_or_kw_arg, *args, kw_arg, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

def func14(pos_arg, /, pos_or_kw_arg, *args, kw_arg=20, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

def func15(pos_arg, /, pos_or_kw_arg=10, *args, kw_arg, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

def func16(pos_arg, /, pos_or_kw_arg=10, *args, kw_arg=20, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs


# METHOD-LIKE

def meth1(self, pos_or_kw_arg, *, kw_arg):
    return pos_or_kw_arg, kw_arg

def meth2(self, pos_or_kw_arg, *, kw_arg=20):
    return pos_or_kw_arg, kw_arg

def meth3(self, pos_or_kw_arg=10, *, kw_arg):
    return pos_or_kw_arg, kw_arg

def meth4(self, pos_or_kw_arg=10, *, kw_arg=20):
    return pos_or_kw_arg, kw_arg

def meth5(self, pos_arg, /, pos_or_kw_arg, *, kw_arg):
    return pos_arg, pos_or_kw_arg, kw_arg

def meth6(self, pos_arg, /, pos_or_kw_arg, *, kw_arg=20):
    return pos_arg, pos_or_kw_arg, kw_arg

def meth7(self, pos_arg, /, pos_or_kw_arg=10, *, kw_arg):
    return pos_arg, pos_or_kw_arg, kw_arg

def meth8(self, pos_arg, /, pos_or_kw_arg=10, *, kw_arg=20):
    return pos_arg, pos_or_kw_arg, kw_arg

def meth9(self, pos_arg, *args, kw_arg, **kwargs):
    return pos_arg, args, kw_arg, kwargs

def meth10(self, pos_arg, *args, kw_arg=20, **kwargs):
    return pos_arg, args, kw_arg, kwargs

def meth11(self, pos_arg=10, *args, kw_arg, **kwargs):
    return pos_arg, args, kw_arg, kwargs

def meth12(self, pos_arg=10, *args, kw_arg=20, **kwargs):
    return pos_arg, args, kw_arg, kwargs

def meth13(self, pos_arg, /, pos_or_kw_arg, *args, kw_arg, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

def meth14(self, pos_arg, /, pos_or_kw_arg, *args, kw_arg=20, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

def meth15(self, pos_arg, /, pos_or_kw_arg=10, *args, kw_arg, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

def meth16(self, pos_arg, /, pos_or_kw_arg=10, *args, kw_arg=20, **kwargs):
    return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs


class Methods:
    def func1(self, pos_or_kw_arg, *, kw_arg):
        return pos_or_kw_arg, kw_arg

    def func2(self, pos_or_kw_arg, *, kw_arg=20):
        return pos_or_kw_arg, kw_arg

    def func3(self, pos_or_kw_arg=10, *, kw_arg):
        return pos_or_kw_arg, kw_arg

    def func4(self, pos_or_kw_arg=10, *, kw_arg=20):
        return pos_or_kw_arg, kw_arg

    def func5(self, pos_arg, /, pos_or_kw_arg, *, kw_arg):
        return pos_arg, pos_or_kw_arg, kw_arg

    def func6(self, pos_arg, /, pos_or_kw_arg, *, kw_arg=20):
        return pos_arg, pos_or_kw_arg, kw_arg

    def func7(self, pos_arg, /, pos_or_kw_arg=10, *, kw_arg):
        return pos_arg, pos_or_kw_arg, kw_arg

    def func8(self, pos_arg, /, pos_or_kw_arg=10, *, kw_arg=20):
        return pos_arg, pos_or_kw_arg, kw_arg

    def func9(self, pos_arg, *args, kw_arg, **kwargs):
        return pos_arg, args, kw_arg, kwargs

    def func10(self, pos_arg, *args, kw_arg=20, **kwargs):
        return pos_arg, args, kw_arg, kwargs

    def func11(self, pos_arg=10, *args, kw_arg, **kwargs):
        return pos_arg, args, kw_arg, kwargs

    def func12(self, pos_arg=10, *args, kw_arg=20, **kwargs):
        return pos_arg, args, kw_arg, kwargs

    def func13(self, pos_arg, /, pos_or_kw_arg, *args, kw_arg, **kwargs):
        return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

    def func14(self, pos_arg, /, pos_or_kw_arg, *args, kw_arg=20, **kwargs):
        return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

    def func15(self, pos_arg, /, pos_or_kw_arg=10, *args, kw_arg, **kwargs):
        return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs

    def func16(self, pos_arg, /, pos_or_kw_arg=10, *args, kw_arg=20, **kwargs):
        return pos_arg, pos_or_kw_arg, args, kw_arg, kwargs


@pytest.mark.parametrize("ofunc, args, kwargs", [
    (func1, (1,), dict(kw_arg=2)),
    (func1, (), dict(pos_or_kw_arg=1, kw_arg=2)),
    (func2, (1,), {}),
    (func3, (), dict(kw_arg=2)),
    (func4, (), {}),
    (func4, (1,), dict(kw_arg=2)),
    (func5, (1, 2), dict(kw_arg=3)),
    (func5, (1,), dict(pos_or_kw_arg=2, kw_arg=3)),
    (func6, (1, 2), {}),
    (func6, (1,), dict(pos_or_kw_arg=2)),
    (func7, (1,), dict(kw_arg=3)),
    (func8, (1,), {}),
    (func9, (1,), dict(kw_arg=2)),
    (func9, (1,2,3,4), dict(kw_arg=5, kw_arg2=6)),
    (func10, (1,), {}),
    (func11, (), dict(kw_arg=2)),
    (func12, (), {}),
    (func13, (1, 2), dict(kw_arg=3)),
    (func14, (1, 2), {}),
    (func15, (1,), dict(kw_arg=3)),
    (func16, (1,), {}),
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
    (Methods, "func1", (1,), dict(kw_arg=2)),
    (Methods, "func1", (), dict(pos_or_kw_arg=1, kw_arg=2)),
    (Methods, "func2", (1,), {}),
    (Methods, "func3", (), dict(kw_arg=2)),
    (Methods, "func4", (), {}),
    (Methods, "func4", (1,), dict(kw_arg=2)),
    (Methods, "func5", (1, 2), dict(kw_arg=3)),
    (Methods, "func5", (1,), dict(pos_or_kw_arg=2, kw_arg=3)),
    (Methods, "func6", (1, 2), {}),
    (Methods, "func6", (1,), dict(pos_or_kw_arg=2)),
    (Methods, "func7", (1,), dict(kw_arg=3)),
    (Methods, "func8", (1,), {}),
    (Methods, "func9", (1,), dict(kw_arg=2)),
    (Methods, "func9", (1,2,3,4), dict(kw_arg=5, kw_arg2=6)),
    (Methods, "func10", (1,), {}),
    (Methods, "func11", (), dict(kw_arg=2)),
    (Methods, "func12", (), {}),
    (Methods, "func13", (1, 2), dict(kw_arg=3)),
    (Methods, "func14", (1, 2), {}),
    (Methods, "func15", (1,), dict(kw_arg=3)),
    (Methods, "func16", (1,), {}),
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
    (meth1, (1,), dict(kw_arg=2)),
    (meth1, (), dict(pos_or_kw_arg=1, kw_arg=2)),
    (meth2, (1,), {}),
    (meth3, (), dict(kw_arg=2)),
    (meth4, (), {}),
    (meth4, (1,), dict(kw_arg=2)),
    (meth5, (1, 2), dict(kw_arg=3)),
    (meth5, (1,), dict(pos_or_kw_arg=2, kw_arg=3)),
    (meth6, (1, 2), {}),
    (meth6, (1,), dict(pos_or_kw_arg=2)),
    (meth7, (1,), dict(kw_arg=3)),
    (meth8, (1,), {}),
    (meth9, (1,), dict(kw_arg=2)),
    (meth9, (1,2,3,4), dict(kw_arg=5, kw_arg2=6)),
    (meth10, (1,), {}),
    (meth11, (), dict(kw_arg=2)),
    (meth12, (), {}),
    (meth13, (1, 2), dict(kw_arg=3)),
    (meth14, (1, 2), {}),
    (meth15, (1,), dict(kw_arg=3)),
    (meth16, (1,), {}),
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
