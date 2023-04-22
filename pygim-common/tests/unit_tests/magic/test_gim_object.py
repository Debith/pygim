# -*- coding: utf-8 -*-
import pytest
import sys
import importlib

def test_gim_object_cannot_be_created(importer):
    # NOTE: Magic is stand-alone package which can be imported separately to avoid
    # execution of parent modules. This has benefit of testing otherwise difficult
    # code (metaclasses!) to test in isolation.
    importer("pygim-common/pygim/utils/__init__.py", execute=True, store=True)
    utils = importlib.import_module("pygim.utils")
    magic = importlib.import_module("pygim.kernel.magic")

    try:
        magic.gim_object()
    except Exception:
        pass
    else:
        assert False


def test_gim_object_creation(importer):
    # NOTE: see above.
    gim_object = importer("pygim-common/pygim/kernel/magic/gim_object.py")

    class Object(gim_object.GimObject):
        """ Empty """
        def __init__(self, data):
            self.data = data

    assert Object(42).data == 42


def test_gim_object_extension(importer):
    # NOTE: see above.
    gim_object = importer("pygim-common/pygim/kernel/magic/gim_object.py").GimObject

    class Object(gim_object):
        """ Empty """
        def __init__(self):
            self.public = 1
            self._protected = 2
            self.__private = 3

        def orig(self):
            return self.public, self._protected, self.__private

    class OtherOne:
        def func1(self):
            return self.public, self._protected, self.__private

    class OtherTwo:
        def func2(self):
            return self.public, self._protected, self.__private

    Object << OtherOne.func1 << OtherTwo.func2

    o = Object()
    assert o.orig() == o.func1() == o.func2()


def test_gim_object_extension_2(importer):
    # NOTE: see above.
    gim_object = importer("pygim-common/pygim/kernel/magic/gim_object.py").GimObject

    class Object(gim_object):
        """ Empty """
        def __init__(self):
            self.public = 1
            self._protected = 2
            self.__private = 3

        def orig(self):
            return self.public, self._protected, self.__private

    class OtherOne:
        def func1(self):
            return self.public, self._protected, self.__private

    class OtherTwo:
        def func2(self):
            return self.public, self._protected, self.__private

    Object << (OtherOne.func1, OtherTwo.func2)

    o = Object()
    assert o.orig() == o.func1() == o.func2()



if __name__ == '__main__':
    pytest.main([__file__])
