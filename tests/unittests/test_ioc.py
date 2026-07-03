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


# Cycle/forward-ref classes live at module level: get_type_hints resolves
# string annotations against module globals, not test-function locals.
class _CycleA:
    def __init__(self, b: "_CycleB"):
        self.b = b


class _CycleB:
    def __init__(self, a: "_CycleA"):
        self.a = a


class _BadHint:
    def __init__(self, x: "NotDefinedAnywhere"):  # noqa: F821
        self.x = x


def test_circular_autowire_is_detected(container):
    container.register(_CycleA, _CycleA, autowire=True)
    container.register(_CycleB, _CycleB, autowire=True)

    with pytest.raises(RuntimeError, match="Circular dependency"):
        container.resolve(_CycleA)


def test_registration_during_resolve_is_safe():
    container = Container()

    class IService:
        pass

    class Service(IService):
        pass

    def registering_provider():
        # Force the internal registry storage to reallocate mid-resolve.
        for i in range(512):
            class Extra:
                pass

            container.register(Extra, Extra, name=f"extra{i}")
        return Service()

    def tag(instance):
        instance.tagged = True
        return instance

    container.register(IService, registering_provider, decorators=[tag])

    resolved = container.resolve(IService)

    assert isinstance(resolved, IService)
    assert resolved.tagged is True
    assert len(container) == 513


def test_resolve_missing_key_error_names_key(container):
    class IService:
        pass

    with pytest.raises(RuntimeError, match="No provider for key.*IService"):
        container.resolve(IService)


def test_describe_missing_key_error_names_key(container):
    class IService:
        pass

    with pytest.raises(RuntimeError, match="No provider for key.*IService"):
        container.describe(IService)


def test_duplicate_error_names_key(container):
    class IService:
        pass

    container.register(IService, lambda: IService())

    with pytest.raises(RuntimeError, match="Duplicate.*IService"):
        container.register(IService, lambda: IService())


def test_autowire_missing_typed_dependency_raises(container):
    class Database:
        pass

    class NeedsDatabase:
        def __init__(self, database: Database):
            self.database = database

    container.register(NeedsDatabase, NeedsDatabase, autowire=True)

    with pytest.raises(RuntimeError, match="No provider registered for autowired dependency 'database'"):
        container.resolve(NeedsDatabase)


def test_autowire_skips_varargs_and_kwargs(container):
    class Dep:
        pass

    class Service:
        def __init__(self, dep: Dep, *args, **kwargs):
            self.dep = dep

    container.register(Dep, Dep)
    container.register(Service, Service, autowire=True)

    assert isinstance(container.resolve(Service).dep, Dep)


def test_autowire_rejects_positional_only(container):
    class Dep:
        pass

    class Service:
        def __init__(self, dep: Dep, /):
            self.dep = dep

    container.register(Dep, Dep)
    container.register(Service, Service, autowire=True)

    with pytest.raises(RuntimeError, match="positional-only parameter 'dep'"):
        container.resolve(Service)


def test_autowire_unresolvable_hint_raises(container):
    container.register(_BadHint, _BadHint, autowire=True)

    with pytest.raises(RuntimeError, match="could not resolve constructor type hints"):
        container.resolve(_BadHint)


def test_autowire_dependency_registered_later_is_injected(container):
    # The default is a sentinel rather than None: on Python <= 3.10,
    # get_type_hints() rewrites `dep: Dep = None` as Optional[Dep] (implicit
    # Optional), which would never match the identity-keyed registration.
    unset = object()

    class Dep:
        pass

    class Service:
        def __init__(self, dep: Dep = unset):
            self.dep = dep

    container.register(Service, Service, autowire=True)
    assert container.resolve(Service).dep is unset  # default fallback

    container.register(Dep, Dep)
    assert isinstance(container.resolve(Service).dep, Dep)  # now injected


def test_autowire_introspection_is_cached(container):
    import inspect as inspect_module
    from unittest.mock import patch

    class Dep:
        pass

    class Service:
        def __init__(self, dep: Dep):
            self.dep = dep

    container.register(Dep, Dep)
    container.register(Service, Service, autowire=True)

    real_signature = inspect_module.signature
    calls = []

    def counting_signature(*args, **kwargs):
        calls.append(args)
        return real_signature(*args, **kwargs)

    with patch("inspect.signature", counting_signature):
        container.resolve(Service)
        container.resolve(Service)

    assert len(calls) == 1  # reflection ran once; the plan replays from cache


def test_registration_decorator_is_reusable():
    container = Container()

    class IService:
        pass

    def tag(instance):
        instance.tagged = True
        return instance

    @container.register(IService, decorators=[tag])
    class First(IService):
        pass

    rereg = container.register(IService, decorators=[tag], override=True)

    class Second(IService):
        pass

    class Third(IService):
        pass

    rereg(Second)
    rereg(Third)  # second use must still carry the decorators

    resolved = container.resolve(IService)
    assert isinstance(resolved, Third)
    assert resolved.tagged is True


def test_registration_decorator_keeps_container_alive():
    import gc

    class IService:
        pass

    decorator = Container().register(IService)  # container is now unreferenced
    gc.collect()

    class Service(IService):
        pass

    assert decorator(Service) is Service


def test_decorator_form_with_override(container):
    class IService:
        pass

    @container.register(IService)
    class First(IService):
        pass

    @container.register(IService, override=True)
    class Second(IService):
        pass

    assert isinstance(container.resolve(IService), Second)


def test_registered_keys_unnamed_uses_none(container):
    class IService:
        pass

    container.register(IService, IService)
    keys = container.registered_keys()

    assert keys[0][0] is IService
    assert keys[0][1] is None


def test_bad_keys_raise(container):
    class IService:
        pass

    with pytest.raises(TypeError):
        container.resolve((IService, "name", "extra"))  # 3-tuple key

    with pytest.raises(TypeError):
        container.register(IService, lambda: IService(), name=123)


def test_capacity_and_repr():
    container = Container(capacity=4)
    assert repr(container) == "Container(size=0)"

    class IService:
        pass

    container.register(IService, lambda: IService())
    assert repr(container) == "Container(size=1)"


def test_service_descriptor_construction_and_lifecycle_property():
    from pygim.ioc import ServiceDescriptor

    class IService:
        pass

    class Service(IService):
        pass

    descriptor = ServiceDescriptor(IService, Service, lifecycle="singleton", name="primary")

    assert descriptor.interface is IService
    assert descriptor.provider is Service
    assert descriptor.lifecycle == "singleton"
    assert descriptor.name == "primary"
    assert descriptor.autowire is False


def test_service_descriptor_is_read_only():
    from pygim.ioc import ServiceDescriptor

    class IService:
        pass

    class Service(IService):
        pass

    descriptor = ServiceDescriptor(IService, Service)

    with pytest.raises(AttributeError):
        descriptor.lifecycle = "singleton"
    with pytest.raises(AttributeError):
        descriptor.provider = Service


def test_describe_returns_read_only_snapshot(container):
    class IService:
        pass

    class Service(IService):
        pass

    container.register(IService, Service, lifecycle="singleton")
    descriptor = container.describe(IService)

    with pytest.raises(AttributeError):
        descriptor.lifecycle = "transient"

    assert container.describe(IService).lifecycle == "singleton"


def test_register_rejects_non_class_interface(container):
    with pytest.raises(TypeError, match="interface must be a class"):
        container.register(42, lambda: 42)

    with pytest.raises(TypeError, match="interface must be a class"):
        container.register(None, lambda: None)


def test_autowire_uninspectable_class_raises(container):
    # `super` is a class but inspect.signature() cannot produce a signature
    # for it, which exercises the "uninspectable provider" branch.
    container.register(super, super, autowire=True)

    with pytest.raises(RuntimeError, match="inspectable class provider"):
        container.resolve(super)


if __name__ == "__main__":
    from pygim.core.testing import run_tests

    run_tests(__file__, Container.__module__, coverage=False)