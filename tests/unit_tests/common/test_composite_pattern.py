# -*- coding: utf-8 -*-

from ast import Call
import pytest

from pygim.magic.composite_pattern import composite_pattern

############################################################
# PARAMETERLESS TESTS
############################################################


def test_creating_components_happen_through_the_component_class():
    TestComponent, TestLeaf, TestComposite = composite_pattern("Test")

    leaf = TestComponent("one")
    composite = TestComponent(["many", "more"])

    assert isinstance(leaf, TestLeaf)
    assert isinstance(composite, TestComposite)


def test_independent_classes_are_independent_even_if_the_name_is_same():
    TestComponent1, TestLeaf1, TestComposite1 = composite_pattern("Test")
    TestComponent2, TestLeaf2, TestComposite2 = composite_pattern("Test")

    # IDs differ
    assert id(TestComponent1) != id(TestComponent2)
    assert id(TestLeaf1) != id(TestLeaf2)
    assert id(TestComposite1) != id(TestComposite2)

    # Names match
    assert TestComponent1.__qualname__ == TestComponent2.__qualname__
    assert TestLeaf1.__qualname__ == TestLeaf2.__qualname__
    assert TestComposite1.__qualname__ == TestComposite2.__qualname__


def test_independent_classes_creates_indpendent_instances_even_if_the_name_is_same():
    TestComponent1, TestLeaf1, TestComposite1 = composite_pattern("Test")
    TestComponent2, TestLeaf2, TestComposite2 = composite_pattern("Test")

    leaf1 = TestComponent1("one")
    composite1 = TestComponent1(["many", "more"])

    leaf2 = TestComponent2("one")
    composite2 = TestComponent2(["many", "more"])

    # IDs differ
    assert id(leaf1) != id(leaf2)
    assert id(composite1) != id(composite2)

    # Instances match their corresponding classes
    assert isinstance(leaf1, TestLeaf1)
    assert isinstance(composite1, TestComposite1)

    assert isinstance(leaf2, TestLeaf2)
    assert isinstance(composite2, TestComposite2)

    # Instances are not confused between classes.
    assert not isinstance(leaf1, TestLeaf2)
    assert not isinstance(composite1, TestComposite2)

    assert not isinstance(leaf2, TestLeaf1)
    assert not isinstance(composite2, TestComposite1)


def test_data_is_iterable():
    TestComponent, TestLeaf, _ = composite_pattern("Test")

    leaf = TestComponent("one")
    composite = TestComponent(["many", "more"])

    # Iteration will always return the leaf object.
    assert list(leaf) == [TestLeaf("one")]
    assert list(composite) == [TestLeaf("many"), TestLeaf("more")]


def test_equality_can_be_tested():
    TestComponent, *_ = composite_pattern("Test")

    leaf1 = TestComponent("one")
    leaf2 = TestComponent("one")
    leaf3 = TestComponent("two")

    # Equality checks among leaf
    assert leaf1 == leaf2
    assert leaf1 != leaf3
    assert leaf2 != leaf3

    composite1 = TestComponent(["many", "more"])
    composite2 = TestComponent(["many", "more"])
    composite3 = TestComponent(["one", "more"])

    # Equality checks among composite
    assert composite1 == composite2
    assert composite1 != composite3
    assert composite2 != composite3


def test_supports_creation_full_hierarchy():
    # L stands for Leaf, C stand for Composite
    TestComponent, L, C = composite_pattern("Test")

    composite = TestComponent([1, 2, [3, 4, [5, 6], 7], 8])
    actual = list(composite)
    expected = [L(1), L(2), C([L(3), L(4), C([L(5), L(6)]), L(7)]), L(8)]

    assert actual == expected
    assert len(actual) == len(expected)


def test_visiting_possible_for_leaf_objects():
    # L stands for Leaf
    TestComponent, L, _ = composite_pattern("Test")
    composite = TestComponent([1, 2, [3, 4, [5, 6], 7], 8])
    visits = composite.visit(lambda l: l)

    assert visits == [1, 2, [3, 4, [5, 6], 7], 8]

#################################################################
# INHERITANCE
#################################################################

def test_inheritance_does_not_confuse_between_subclasses():
    TestComponent, TestLeaf, TestComposite = composite_pattern("Test")

    class SubComponent(TestComponent):
        pass

    class SubLeaf(TestLeaf):
        pass

    class SubComposite(TestComposite):
        pass

    # IDs differ
    assert id(TestComponent) == id(SubComponent)
    assert id(TestLeaf) == id(SubLeaf)
    assert id(TestComposite) == id(SubComposite)


def test_inheritance_does_not_confuse_between_instances_of_subclasses():
    TestComponent, TestLeaf, TestComposite = composite_pattern("Test")

    class SubComponent(TestComponent):
        pass

    class SubLeaf(TestLeaf):
        pass

    class SubComposite(TestComposite):
        pass

    # The base class always creates the subclass objects.
    assert isinstance(TestComponent(1), TestLeaf)
    assert isinstance(TestComponent(1), SubLeaf)
    assert SubComponent(1) == SubLeaf(1)

    assert isinstance(TestComponent([1, 2]), TestComposite)
    assert isinstance(TestComponent([1, 2]), SubComposite)
    assert SubComponent([1, 2]) == SubComposite([SubLeaf(1), SubLeaf(2)])


def test_comparing_occurs_between_own_subclasses():
    TestComponent, TestLeaf, TestComposite = composite_pattern("Test")

    class SubComponent(TestComponent):
        pass

    class SubLeaf(TestLeaf):
        pass

    class SubComposite(TestComposite):
        pass

    # Should compare apples to apples
    assert TestLeaf(1) == TestLeaf(1)
    assert SubLeaf(1) == SubLeaf(1)
    assert TestLeaf(1) == SubLeaf(1)

    # Should compare apples to apples
    assert TestComposite([1]) == TestComposite([1])
    assert SubComposite([1]) == SubComposite([1])
    assert TestComposite([1]) == SubComposite([1])


def creating_nested_leaves_is_not_possible():
    TestComponent, TestLeaf, _ = composite_pattern("Test")

    t1 = TestLeaf("hello")
    t2 = TestLeaf(t1)
    t3 = TestComponent(TestLeaf(t1))

    assert id(t1) == id(t2)
    assert t1 == t2
    assert id(t3) == id(t1)


def test_comparing_occurs_between_own_subclasses_with_multiple_inheritance():
    TestComponent, TestLeaf, TestComposite = composite_pattern("Test")

    class SubLeaf1(TestLeaf):
        pass

    class SubLeaf2(TestLeaf):
        pass

    s1 = SubLeaf1(1)
    s2 = SubLeaf2(1)

    assert s1 != s2




def test_it_is_possible_to_add_new_components():
    TestComponent, TestLeaf, TestComposite = composite_pattern("Test")

    c = TestComposite([])
    assert list(c) == []

    c.add("component")
    assert list(c) == [TestLeaf("component")]

    c.add(["c1", "c2"])
    assert list(c) == [TestLeaf("component"), TestComposite([TestLeaf("c1"), TestLeaf("c2")])]

    c.reset()
    assert list(c) == []

    c.add(TestLeaf(1))
    c.add(TestLeaf(2))
    assert list(c) == [TestLeaf(1), TestLeaf(2)]

    c.reset()
    c.add(TestComposite([TestLeaf(1) ,TestLeaf(2)]))
    c.add(TestComposite([1 ,2]))
    assert list(c)[0] == list(c)[-1]



if __name__ == "__main__":
    pytest.main([__file__])