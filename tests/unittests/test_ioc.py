import pytest

from pygim import Container as TopLevelContainer
from pygim.ioc import Container


@pytest.fixture
def container():
    return Container()


def test_public_reexport():
    assert TopLevelContainer is Container


def test_register_and_resolve_transient(container):
    class IService:
        pass

    class Service(IService):
        pass

    container.register(IService, Service)

    first = container.resolve(IService)
    second = container[IService]

    assert isinstance(first, IService)
    assert isinstance(second, IService)
    assert first is not second
    assert IService in container
    assert len(container) == 1


def test_named_registration_and_registered_keys(container):
    class IService:
        pass

    class Service(IService):
        pass

    container.register(IService, Service, name="default")

    resolved = container.resolve((IService, "default"))
    keys = container.registered_keys()

    assert isinstance(resolved, IService)
    assert len(keys) == 1
    assert keys[0][0] is IService
    assert keys[0][1] == "default"


def test_singleton_lifecycle_caches_instance(container):
    class IService:
        pass

    class Service(IService):
        pass

    container.register(IService, Service, lifecycle="singleton")

    first = container.resolve(IService)
    second = container.resolve(IService)

    assert first is second


def test_decorators_are_applied(container):
    class IService:
        pass

    class Service(IService):
        def __init__(self):
            self.steps = ["provider"]

    def decorate(instance):
        instance.steps.append("decorator")
        return instance

    container.register(IService, Service, decorators=[decorate])

    resolved = container.resolve(IService)
    assert resolved.steps == ["provider", "decorator"]


def test_decorator_registration_form():
    container = Container()

    class IService:
        pass

    @container.register(IService, lifecycle="singleton")
    class Service(IService):
        pass

    assert isinstance(container.resolve(IService), IService)
    assert container.resolve(IService) is container.resolve(IService)


def test_autowire_class_provider_by_type_hint(container):
    class Repository:
        pass

    class MemoryRepository(Repository):
        pass

    class Service:
        def __init__(self, repository: Repository):
            self.repository = repository

    container.register(Repository, MemoryRepository, lifecycle="singleton")
    container.register(Service, Service, autowire=True)

    resolved = container.resolve(Service)

    assert isinstance(resolved, Service)
    assert isinstance(resolved.repository, Repository)
    assert resolved.repository is container.resolve(Repository)


def test_autowire_uses_defaults_for_missing_typed_dependency(container):
    class Repository:
        pass

    class MemoryRepository(Repository):
        pass

    class Service:
        def __init__(self, repository: Repository, retries: int = 3):
            self.repository = repository
            self.retries = retries

    container.register(Repository, MemoryRepository)
    container.register(Service, Service, autowire=True)

    resolved = container.resolve(Service)

    assert isinstance(resolved.repository, Repository)
    assert resolved.retries == 3


def test_duplicate_without_override_raises(container):
    class IService:
        pass

    class First(IService):
        pass

    class Second(IService):
        pass

    container.register(IService, First)

    with pytest.raises(RuntimeError):
        container.register(IService, Second)


def test_override_missing_raises(container):
    class IService:
        pass

    class Service(IService):
        pass

    with pytest.raises(RuntimeError):
        container.register(IService, Service, override=True)


def test_override_replaces_provider_and_clears_singleton(container):
    class IService:
        pass

    class First(IService):
        pass

    class Second(IService):
        pass

    container.register(IService, First, lifecycle="singleton")
    original = container.resolve(IService)

    container.register(IService, Second, lifecycle="singleton", override=True)
    replacement = container.resolve(IService)

    assert isinstance(replacement, Second)
    assert replacement is not original
    assert replacement is container.resolve(IService)


def test_invalid_inputs_raise(container):
    class IService:
        pass

    with pytest.raises(TypeError):
        container.register(IService, 123)

    with pytest.raises(TypeError):
        container.register(IService, lambda: object(), decorators=[123])

    with pytest.raises(TypeError):
        container.register(IService, lambda: object(), autowire=True)

    with pytest.raises(RuntimeError):
        container.register(IService, lambda: object(), lifecycle="scoped")


def test_autowire_requires_annotations_when_no_default(container):
    class Service:
        def __init__(self, dependency):
            self.dependency = dependency

    container.register(Service, Service, autowire=True)

    with pytest.raises(RuntimeError):
        container.resolve(Service)


def test_resolve_enforces_interface_after_provider(container):
    class IService:
        pass

    container.register(IService, lambda: object())

    with pytest.raises(RuntimeError):
        container.resolve(IService)


def test_resolve_enforces_interface_after_decorators(container):
    class IService:
        pass

    class Service(IService):
        pass

    def break_interface(_instance):
        return object()

    container.register(IService, Service, decorators=[break_interface])

    with pytest.raises(RuntimeError):
        container.resolve(IService)


def test_describe_returns_service_descriptor(container):
    class IService:
        pass

    class Service(IService):
        pass

    container.register(IService, Service, name="primary", lifecycle="singleton")

    descriptor = container.describe((IService, "primary"))

    assert descriptor.interface is IService
    assert descriptor.provider is Service
    assert descriptor.lifecycle == "singleton"
    assert descriptor.name == "primary"


def test_describe_includes_autowire_flag(container):
    class Repository:
        pass

    class MemoryRepository(Repository):
        pass

    class Service:
        def __init__(self, repository: Repository):
            self.repository = repository

    container.register(Repository, MemoryRepository)
    container.register(Service, Service, autowire=True)

    descriptor = container.describe(Service)

    assert descriptor.autowire is True


if __name__ == "__main__":
    from pygim.core.testing import run_tests

    run_tests(__file__, Container.__module__, coverage=False)