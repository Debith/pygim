# -*- coding: utf-8 -*-
from types import FunctionType, MethodWrapperType

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
    assert actual == expected


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

def func1(pos_arg, *, kw_arg):
    return pos_arg, kw_arg

def func2(pos_arg, *, kw_arg=20):
    return pos_arg, kw_arg

def func3(pos_arg=10, *, kw_arg):
    return pos_arg, kw_arg

def func4(pos_arg=10, *, kw_arg=20):
    return pos_arg, kw_arg

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


class Methods:
    def func1(self, pos_arg, *, kw_arg):
        return pos_arg, kw_arg

    def func2(self, pos_arg, *, kw_arg=20):
        return pos_arg, kw_arg

    def func3(self, pos_arg=10, *, kw_arg):
        return pos_arg, kw_arg

    def func4(self, pos_arg=10, *, kw_arg=20):
        return pos_arg, kw_arg

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
    (func4, (), {}),
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


    # for name in dir(function):
    #     if isinstance(getattr(function, name), (FunctionType, MethodWrapperType)):
    #         continue
    #     if name in ("__call__", "__delattr__", "__dir__", "__eq__", "__format__",):  # Ignore these as they won't match
    #         continue

    #     actual_value = getattr(new_function, name)
    #     expected_value = getattr(function, name)

    #     not_matching = []

    #     if actual_value != expected_value:
    #         not_matching.append(dict(name=name, actual_value=actual_value, expected_value=expected_value))

    #     if not_matching:
    #         assert False, not_matching


if __name__ == '__main__':
    pytest.main([__file__])
