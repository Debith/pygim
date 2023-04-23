# -*- coding: utf-8 -*-
import pytest

class OtherOne:
    def func1(self):
        return self.public, self._protected, self.__private

class OtherTwo:
    def func2(self):
        return self.public, self._protected, self.__private


def test_gim_object_cannot_be_created(importer):
    # NOTE: Magic is stand-alone package which can be imported separately to avoid
    # execution of parent modules. This has benefit of testing otherwise difficult
    # code (metaclasses!) to test in isolation.
    from _pygim._magic import GimObject

    try:
        GimObject()
    except Exception:
        pass
    else:
        assert False


def test_gim_object_creation(importer):
    # NOTE: see above.
    from _pygim._magic import GimObject

    class Object(GimObject):
        """ Empty """
        def __init__(self, data):
            self.data = data

    assert Object(42).data == 42


def test_gim_object_extension(importer):
    # NOTE: see above.
    from _pygim._magic import GimObject

    class Object(GimObject):
        """ Empty """
        def __init__(self):
            self.public = 1
            self._protected = 2
            self.__private = 3

        def orig(self):
            return self.public, self._protected, self.__private


    Object << OtherOne.func1 << OtherTwo.func2

    o = Object()
    assert o.orig() == o.func1() == o.func2()


def test_gim_object_extension_2(importer):
    # NOTE: see above.
    from _pygim._magic import GimObject

    class Object(GimObject):
        """ Empty """
        def __init__(self):
            self.public = 1
            self._protected = 2
            self.__private = 3

        def orig(self):
            return self.public, self._protected, self.__private

    Object << (OtherOne.func1, OtherTwo.func2)

    o = Object()
    assert o.orig() == o.func1() == o.func2()


def test_gim_object_extension_via_class(importer):
    # NOTE: see above.
    from _pygim._magic import GimObject

    class Object(GimObject):
        """ Empty """
        def __init__(self):
            self.public = 1
            self._protected = 2
            self.__private = 3

        def orig(self):
            return self.public, self._protected, self.__private

    Object << OtherOne << OtherTwo

    o = Object()
    assert o.orig() == o.func1() == o.func2()


if __name__ == '__main__':
    pytest.main([__file__])
