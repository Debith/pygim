import pytest
from pygim import Factory, Unset
from pygim.ddd.repository import ddd_factory
from pygim.system.exceptions import FactoryMethodRegisterationException
from pygim.system import factory

### Test dynamic registeration of factory methods.

object_factory = Factory('test.TestFactory')


@object_factory.register
def create_test_object():
    return "Instance Object"


def create_rock():
    return "Rock"


def create_paper():
    return "Paper"


def create_cannon():
    return "Cannon"


@pytest.mark.parametrize('name,namespace,expected_name', [
    ('namespace.test', Unset, "namespace.test"),
    ('test', 'namespace', "namespace.test"),
    ('namespace.middle.test', Unset, "namespace.middle.test"),
    ('middle.test', "namespace", "namespace.middle.test"),
    ('test', 'namespace.middle', "namespace.middle.test")
])
def test_valid_naming_combinations(name, namespace, expected_name):
    if namespace is not Unset:
        new_factory = Factory(name, namespace=namespace)
    else:
        new_factory = Factory(name)

    expected_factory = Factory(expected_name)

    if id(new_factory) != id(expected_factory):
        assert id(new_factory) == id(Factory(expected_name))


def test_namespaces_makes_factories_independent():
    object_factory_1 = Factory("factory", namespace="first")
    object_factory_2 = Factory("factory", namespace="second")
    object_factory_3 = Factory("factory2", namespace="second")

    assert id(object_factory_1) != id(object_factory_2) != id(object_factory_3)


def test_factories_with_same_name():
    """
    Instances of factories containing different name are always unique. Even instances
    of the same class are different, if the name is different.
    """
    class DummyFactory1(Factory):
        def create_something(self):
            return


    class DummyFactory2(Factory):
        def create_something(self):
            return

    assert id(DummyFactory1) != id(DummyFactory2)

    dummy_factory1 = DummyFactory1()
    dummy_factory2 = DummyFactory2()
    dummy_factory3 = DummyFactory2()
    dummy_factory4 = DummyFactory2("Additional Name")

    assert id(dummy_factory1) != id(dummy_factory2)
    assert id(dummy_factory2) == id(dummy_factory3)
    assert id(dummy_factory3) != id(dummy_factory4)


def test_create_object_via_function():
    actual_text = object_factory.create_test_object()
    if actual_text != "Instance Object":
        assert False


def test_create_function_names_are_enforced():
    def does_not_start_with_create():
        pass

    with pytest.raises(FactoryMethodRegisterationException):
        object_factory.register(does_not_start_with_create)


def test_registered_methods_stay_with_factories_of_same_name():
    new_factory1 = Factory('test.new')
    new_factory1.register(create_test_object)

    new_factory2 = Factory('test.new')
    actual_text = new_factory2.create_test_object()

    if actual_text != "Instance Object":
        assert False


def test_creating_factory_with_object_map():
    my_factory = Factory('test.objects', rock=create_rock,
                                         sheet=create_paper,
                                         cannon=create_cannon,
                                         )

    results = [
        my_factory['rock'](),
        my_factory.create_rock(),
        my_factory['sheet'](),      # Aliased name works too!
        my_factory['paper'](),
        my_factory.create_paper(),
        my_factory['cannon'](),
        my_factory.create_cannon(),
    ]

    assert results == ['Rock', 'Rock', 'Paper', 'Paper', 'Paper', 'Cannon', 'Cannon']


if __name__ == "__main__":
    from pygim.testing import run_tests

    # With coverage run, tests fail in meta.__call__ due to reload.
    run_tests(__file__, factory, coverage=True)