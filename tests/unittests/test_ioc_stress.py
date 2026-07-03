"""Stress, negative, and odd-situation tests for pygim.ioc.Container.

These poke the container from awkward angles: failing providers and
decorators, re-entrant registration and resolution, identity-keyed edge
cases, protocol/ABC interfaces, hostile inputs, and deep/wide/large
dependency graphs. The behaviors pinned here are load-bearing contracts.
"""

import gc

import pytest

from pygim.ioc import Container


@pytest.fixture
def container():
    return Container()


# Module level: get_type_hints resolves string annotations against module
# globals, so self-referential hints cannot live inside test functions.
class _Selfish:
    def __init__(self, other: "_Selfish"):
        self.other = other


def _make_layer(previous):
    if previous is None:
        class Layer:
            pass
    else:
        class Layer:
            def __init__(self, dep: previous):
                self.dep = dep
    return Layer


# ---------------------------------------------------------------------------
# Failing providers and decorators
# ---------------------------------------------------------------------------


def test_provider_exception_keeps_original_type(container):
    class IService:
        pass

    def exploding():
        raise ValueError("boom")

    container.register(IService, exploding)

    with pytest.raises(ValueError, match="boom"):
        container.resolve(IService)


def test_singleton_is_not_poisoned_by_failing_first_resolve(container):
    class IService:
        pass

    class Service(IService):
        pass

    attempts = []

    def flaky():
        attempts.append(1)
        if len(attempts) == 1:
            raise RuntimeError("first attempt fails")
        return Service()

    container.register(IService, flaky, lifecycle="singleton")

    with pytest.raises(RuntimeError, match="first attempt fails"):
        container.resolve(IService)

    resolved = container.resolve(IService)  # retried, then cached
    assert isinstance(resolved, IService)
    assert container.resolve(IService) is resolved
    assert len(attempts) == 2


def test_failing_decorator_does_not_cache_singleton(container):
    class IService:
        pass

    class Service(IService):
        pass

    calls = []

    def bad_then_good(instance):
        calls.append(1)
        if len(calls) == 1:
            raise ValueError("decorator failed")
        return instance

    container.register(IService, Service, lifecycle="singleton", decorators=[bad_then_good])

    with pytest.raises(ValueError, match="decorator failed"):
        container.resolve(IService)

    assert isinstance(container.resolve(IService), IService)


def test_decorator_returning_none_fails_validation(container):
    class IService:
        pass

    class Service(IService):
        pass

    container.register(IService, Service, decorators=[lambda instance: None])

    with pytest.raises(RuntimeError, match="interface"):
        container.resolve(IService)


# ---------------------------------------------------------------------------
# Re-entrancy
# ---------------------------------------------------------------------------


def test_provider_overriding_its_own_registration_mid_resolve(container):
    class IService:
        pass

    class Original(IService):
        pass

    class Replacement(IService):
        pass

    def self_replacing():
        container.register(IService, Replacement, lifecycle="singleton", override=True)
        return Original()

    container.register(IService, self_replacing, lifecycle="singleton")

    first = container.resolve(IService)
    assert isinstance(first, Original)  # this resolve ran the old provider

    # The Original instance must NOT have been cached for the overridden
    # registration; the next resolve runs the replacement provider.
    second = container.resolve(IService)
    assert isinstance(second, Replacement)
    assert container.resolve(IService) is second


def test_decorator_resolving_same_key_detects_cycle(container):
    class IService:
        pass

    class Service(IService):
        pass

    def greedy(instance):
        container.resolve(IService)  # still mid-resolve of this very key
        return instance

    container.register(IService, Service, decorators=[greedy])

    with pytest.raises(RuntimeError, match="Circular dependency"):
        container.resolve(IService)


def test_decorator_resolving_other_key_is_fine(container):
    class ILog:
        pass

    class Log(ILog):
        pass

    class IService:
        pass

    class Service(IService):
        pass

    def attach_log(instance):
        instance.log = container.resolve(ILog)
        return instance

    container.register(ILog, Log)
    container.register(IService, Service, decorators=[attach_log])

    assert isinstance(container.resolve(IService).log, ILog)


def test_autowire_self_dependency_detects_cycle(container):
    container.register(_Selfish, _Selfish, autowire=True)

    with pytest.raises(RuntimeError, match="Circular dependency"):
        container.resolve(_Selfish)


# ---------------------------------------------------------------------------
# Key identity and naming edge cases
# ---------------------------------------------------------------------------


def test_empty_string_name_is_distinct_from_unnamed(container):
    class IService:
        pass

    class Unnamed(IService):
        pass

    class Empty(IService):
        pass

    container.register(IService, Unnamed)
    container.register(IService, Empty, name="")

    assert type(container.resolve(IService)) is Unnamed
    assert type(container.resolve((IService, ""))) is Empty
    assert len(container) == 2


def test_unicode_registration_names(container):
    class IService:
        pass

    class Service(IService):
        pass

    container.register(IService, Service, name="サービス-🎁")

    assert isinstance(container.resolve((IService, "サービス-🎁")), IService)
    with pytest.raises(RuntimeError, match="No provider"):
        container.resolve((IService, "サービス"))


def test_tuple_key_with_none_name_equals_bare_interface(container):
    class IService:
        pass

    class Service(IService):
        pass

    container.register(IService, Service)

    assert (IService, None) in container
    assert isinstance(container.resolve((IService, None)), IService)


def test_identical_qualnames_are_distinct_keys(container):
    def make():
        class Service:
            pass

        return Service

    first, second = make(), make()
    assert first.__qualname__ == second.__qualname__

    container.register(first, first)
    container.register(second, second)

    assert type(container.resolve(first)) is first
    assert type(container.resolve(second)) is second


def test_registration_keeps_interface_alive():
    container = Container()

    class IService:
        pass

    class Service(IService):
        pass

    container.register(IService, Service)
    del IService, Service
    gc.collect()

    keys = container.registered_keys()
    assert len(keys) == 1
    interface = keys[0][0]
    assert isinstance(container.resolve(interface), interface)


def test_missing_key_error_with_non_class_key(container):
    # ints have no __qualname__; the error message falls back to repr().
    with pytest.raises(RuntimeError, match="No provider for key.*42"):
        container.resolve(42)


# ---------------------------------------------------------------------------
# Protocols, ABCs, and unusual providers
# ---------------------------------------------------------------------------


def test_runtime_checkable_protocol_as_interface(container):
    from typing import Protocol, runtime_checkable

    @runtime_checkable
    class Sender(Protocol):
        def send(self, message): ...

    class Smtp:
        def send(self, message):
            return f"sent {message}"

    class Broken:
        pass

    container.register(Sender, Smtp)
    assert container.resolve(Sender).send("x") == "sent x"

    container.register(Sender, Broken, override=True)
    with pytest.raises(RuntimeError, match="interface"):
        container.resolve(Sender)


def test_non_runtime_checkable_protocol_fails_loudly(container):
    from typing import Protocol

    class Sender(Protocol):
        def send(self, message): ...

    class Smtp:
        def send(self, message):
            return message

    container.register(Sender, Smtp)

    # isinstance() against a non-runtime_checkable Protocol raises TypeError;
    # the container must surface it unchanged, not swallow or rewrap it.
    with pytest.raises(TypeError):
        container.resolve(Sender)


def test_abc_virtual_subclass_passes_validation(container):
    import abc

    class Storage(abc.ABC):
        pass

    class Disk:  # not a real subclass
        pass

    Storage.register(Disk)
    container.register(Storage, Disk)

    assert isinstance(container.resolve(Storage), Storage)


def test_partial_and_bound_method_providers(container):
    import functools

    class IService:
        pass

    class Service(IService):
        def __init__(self, tag):
            self.tag = tag

    container.register(IService, functools.partial(Service, tag="partial"))
    assert container.resolve(IService).tag == "partial"

    class Builder:
        def build(self):
            return Service(tag="bound")

    container.register(IService, Builder().build, name="bound")
    assert container.resolve((IService, "bound")).tag == "bound"


# ---------------------------------------------------------------------------
# Autowiring corner cases
# ---------------------------------------------------------------------------


def test_autowire_ignores_named_registrations(container):
    class Repo:
        pass

    class NamedRepo(Repo):
        pass

    class Service:
        def __init__(self, repo: Repo = None):
            self.repo = repo

    container.register(Repo, NamedRepo, name="only-named")
    container.register(Service, Service, autowire=True)

    # Autowiring matches unnamed registrations only; the named variant is
    # invisible to it, so the default applies. (Documents current behavior.)
    assert container.resolve(Service).repo is None


def test_autowire_optional_annotation_falls_back_to_default(container):
    from typing import Optional

    class Repo:
        pass

    class Service:
        def __init__(self, repo: Optional[Repo] = None):
            self.repo = repo

    container.register(Repo, Repo)
    container.register(Service, Service, autowire=True)

    # Optional[Repo] is a typing.Union object, not the registered class, so
    # it does not match and the default is used. (Documents current behavior.)
    assert container.resolve(Service).repo is None


def test_error_chain_names_dependency_and_resolving_frame(container):
    class Repo:
        pass

    class Service:
        def __init__(self, repo: Repo):
            self.repo = repo

    container.register(Service, Service, autowire=True)

    with pytest.raises(RuntimeError) as excinfo:
        container.resolve(Service)

    message = str(excinfo.value)
    assert "repo" in message  # the failing dependency parameter
    assert "Service" in message  # the frame being resolved


def test_diamond_dependency_shares_singleton_without_false_cycle(container):
    class D:
        pass

    class B:
        def __init__(self, d: D):
            self.d = d

    class C:
        def __init__(self, d: D):
            self.d = d

    class A:
        def __init__(self, b: B, c: C):
            self.b, self.c = b, c

    container.register(D, D, lifecycle="singleton")
    for cls in (B, C, A):
        container.register(cls, cls, autowire=True)

    a = container.resolve(A)
    assert a.b.d is a.c.d


# ---------------------------------------------------------------------------
# Scale
# ---------------------------------------------------------------------------


def test_deep_autowire_chain(container):
    depth = 40
    layer = None
    for _ in range(depth):
        layer = _make_layer(layer)
        container.register(layer, layer, autowire=True)

    node = container.resolve(layer)
    walked = 0
    while hasattr(node, "dep"):
        node = node.dep
        walked += 1
    assert walked == depth - 1


def test_wide_autowire_graph(container):
    width = 32
    deps = []
    for i in range(width):
        dep = type(f"Dep{i}", (), {})
        deps.append(dep)
        container.register(dep, dep, lifecycle="singleton")

    arglist = ", ".join(f"d{i}" for i in range(width))
    namespace = {}
    exec(f"def __init__(self, {arglist}):\n    self.deps = [{arglist}]", namespace)
    init = namespace["__init__"]
    init.__annotations__ = {f"d{i}": deps[i] for i in range(width)}
    wide = type("Wide", (), {"__init__": init})

    container.register(wide, wide, autowire=True)

    resolved = container.resolve(wide)
    assert len(resolved.deps) == width
    assert all(type(d) is cls for d, cls in zip(resolved.deps, deps))


def test_many_registrations_and_resolves():
    container = Container(capacity=2048)
    interfaces = []
    for i in range(2000):
        cls = type(f"Service{i}", (), {})
        interfaces.append(cls)
        container.register(cls, cls)

    assert len(container) == 2000
    assert len(container.registered_keys()) == 2000
    for cls in interfaces[::97]:
        assert type(container.resolve(cls)) is cls


if __name__ == "__main__":
    from pygim.core.testing import run_tests

    run_tests(__file__, Container.__module__, coverage=False)
