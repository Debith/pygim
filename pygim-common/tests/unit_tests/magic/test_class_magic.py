# -*- coding: utf-8 -*-
import pytest

def xtest_adding_trait_function_via_class(importer):
    # Import module from magic directly.
    from _pygim._magic._traits import Relocator

    relocator = Relocator()

    class First:
        def first(self): pass

    class Second:
        def second(self): pass

    relocator(First, Second, ['second'])

    assert hasattr(First, "first")
    assert hasattr(First, "second")

    assert hasattr(Second, "second")
    assert not hasattr(Second, "first")


def xtest_added_functions_are_truly_part_of_the_new_class(importer):
    class First:
        def __init__(self):
            self.public = 42
            self._protected = 43
            self.__private = 44

        def first(self):
            return self.public + self._protected + self.__private

    class Second:
        def __init__(self):
            self.public = 1000
            self._protected = 100
            self.__private = 10

        def second(self):
            return self.public - self._protected - self.__private

    from _pygim._magic._traits import Relocator

    relocator = Relocator()
    relocator(First, Second, ['second'])

    first_inst = First()
    assert first_inst.first() == 42 + 43 + 44
    assert first_inst.second() == 42 - 43 - 44



def test_combiner_merges_multiple_classes_elegantly(importer):

    class First:
        def first(self): pass

    class Second:
        def second(self): pass

    class Third:
        def third_a(self): pass
        def third_b(self): pass

    from _pygim._magic._traits import combine
    CombinedClass = combine(First, Second, Third)

    assert hasattr(CombinedClass, "first")
    assert hasattr(CombinedClass, "second")
    assert hasattr(CombinedClass, "third_a")
    assert hasattr(CombinedClass, "third_b")

    assert not hasattr(First, "second")
    assert not hasattr(Second, "first")
    assert not hasattr(Third, "first")



def test_combiner_merges_multiple_functions_elegantly(importer):

    class First:
        def first(self): pass

    class Second:
        def second(self): pass

    class Third:
        def third_a(self): pass
        def third_b(self): pass

    from _pygim._magic._traits import combine
    CombinedClass = combine(First.first, Second.second, Third.third_a, Third.third_b)

    assert hasattr(CombinedClass, "first")
    assert hasattr(CombinedClass, "second")
    assert hasattr(CombinedClass, "third_a")
    assert hasattr(CombinedClass, "third_b")

    assert not hasattr(First, "second")
    assert not hasattr(Second, "first")
    assert not hasattr(Third, "first")


if __name__ == '__main__':
    pytest.main([__file__])
