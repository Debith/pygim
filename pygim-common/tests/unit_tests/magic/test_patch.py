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
    mcode_obj.rename_owner("Test", "New")
    new_mcodeobj = mcode_obj.freeze()

    assert id(Test.my_func.__code__) != new_mcodeobj
    assert Test.my_func.__code__ != new_mcodeobj

    expected = patch.MutableCodeObject(Test.my_func.__code__)
    expected['co_names'] = ('public', '_protected', '_New__private')
    actual = patch.MutableCodeObject(new_mcodeobj)
    assert actual == expected



if __name__ == '__main__':
    pytest.main([__file__])
