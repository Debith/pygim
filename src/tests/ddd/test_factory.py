import pytest
from pygim import create_factory

ExampleFactory = create_factory("Example")


@ExampleFactory.register
def create_test_object():
    return "Instance Object"


def create_rock():
    return "Rock"


def create_paper():
    return "Paper"


def create_cannon():
    return "Cannon"


def test_create_object_via_function():
    actual_text = ExampleFactory.create_test_object()
    if actual_text != "Instance Object":
        assert False


def test_create_alternative_factory():
    new_factory = create_factory('Another')

    if new_factory is None:
        assert False

    if id(new_factory) == id(ExampleFactory):
        assert False


def test_create_alternative_factory_again():
    new_factory1 = create_factory('new')
    new_factory2 = create_factory('new')

    if id(new_factory1) != id(new_factory2):
        assert False


def test_registered_methods_stay_with_factories_of_same_name():
    new_factory1 = create_factory('new')
    new_factory1.register(create_test_object)

    new_factory2 = create_factory('new')
    actual_text = new_factory2.create_test_object()

    if actual_text != "Instance Object":
        assert False


def test_creating_factory_with_object_map():
    ObjectFactory = create_factory('objects', dict(rock=create_rock,
                                                   paper=create_paper,
                                                   cannon=create_cannon,
                                                   ))

    if ObjectFactory['rock']() != "Rock":
        assert False


if __name__ == "__main__":
    pytest.main([])