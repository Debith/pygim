# -*- coding: utf-8 -*-
import pytest

def test_mutable_code_object_returns_identical_code_object_for_function(importer):
    # NOTE: Magic is stand-alone package which can be imported separately to avoid
    # execution of parent modules. This has benefit of testing otherwise difficult
    # code (metaclasses!) to test in isolation.
    patch = importer("pygim-common/pygim/kernel/magic/patch.py")

    def my_func(test: int):
        return test * 2

    mutable = patch.MutableCodeObject(my_func.__code__)
    new_codeobj = mutable.freeze()

    assert id(my_func.__code__) != new_codeobj
    assert my_func.__code__ == new_codeobj


def test_mutable_code_object_can_modify_owner_of_class(importer):
    # NOTE: see above
    patch = importer("pygim-common/pygim/kernel/magic/patch.py")

    class Test:
        def __init__(self):
            self.public = 1
            self._protected = 2
            self.__private = 3

        def my_func(self):
            return self.public, self._protected, self.__private

    mcode_obj = patch.MutableCodeObject(Test.my_func.__code__)
    mcode_obj.rename_owner("New")
    new_mcodeobj = mcode_obj.freeze()

    assert id(Test.my_func.__code__) != new_mcodeobj
    assert Test.my_func.__code__ != new_mcodeobj

    expected = patch.MutableCodeObject(Test.my_func.__code__)
    expected['co_names'] = ('public', '_protected', '_New__private')
    actual = patch.MutableCodeObject(new_mcodeobj)
    assert actual == expected


def test_new_transferred_method_behaves_as_a_member_of_new_class(importer):
    # NOTE: see above
    patch = importer("pygim-common/pygim/kernel/magic/patch.py")

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

    trait_func = patch.MutableFuncObject(Test.my_func)
    trait_func >> NewOwner

    instance = NewOwner()
    assert instance.my_func() == instance.original()
    assert instance.my_func.__self__ == instance.original.__self__


def test_new_transferred_function_behaves_as_a_member_of_new_class(importer):
    # NOTE: see above
    patch = importer("pygim-common/pygim/kernel/magic/patch.py")

    def my_func(self):
        return self.public, self._protected, self.__private

    class NewOwner:
        def __init__(self):
            self.public = 1
            self._protected = 2
            self.__private = 3

        def original(self):
            return self.public, self._protected, self.__private

    trait_func = patch.MutableFuncObject(my_func)
    trait_func >> NewOwner

    instance = NewOwner()
    assert instance.my_func() == instance.original()
    assert instance.my_func.__self__ == instance.original.__self__


def test_move_multiple_functions_at_once(importer):
    # NOTE: see above
    patch = importer("pygim-common/pygim/kernel/magic/patch.py")

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

    patch.transfer_ownership(NewOwner,
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


if __name__ == '__main__':
    pytest.main([__file__])
