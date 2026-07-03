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
    """A provider that raises must surface the exception unchanged.

    The container wraps its own errors with key context, so there is a real
    risk of accidentally catching and rewrapping user exceptions as
    RuntimeError along the way. This pins that a ValueError from inside a
    provider reaches the caller as a ValueError with its original message.
    """
    class IService:
        pass

    def exploding():
        raise ValueError("boom")

    container.register(IService, exploding)

    with pytest.raises(ValueError, match="boom"):
        container.resolve(IService)


def test_singleton_is_not_poisoned_by_failing_first_resolve(container):
    """A singleton whose first construction fails must retry on the next resolve.

    Nothing may be cached when the provider throws: caching a half-built or
    absent instance would make one transient failure (e.g. a network blip in
    a connection provider) permanent for the container's lifetime. The second
    resolve must invoke the provider again, and only a successful result gets
    cached.
    """
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
    """A failing decorator must abort the resolve without caching anything.

    Singleton caching happens only after the full provider -> decorators ->
    validation pipeline succeeds. If a decorator throws, the instance it was
    wrapping must be discarded; otherwise a later resolve would return an
    undecorated (or partially decorated) singleton.
    """
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
    """Interface validation runs on the *final* object, after all decorators.

    A buggy decorator that forgets to return the instance (a classic Python
    mistake) yields None. The post-decoration isinstance check must catch
    this and fail the resolve loudly, rather than handing None to the caller.
    """
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
    """Overriding a registration from inside its own provider must not corrupt state.

    This is the generation-guard contract: the in-flight resolve still returns
    the instance the old provider built, but that instance must NOT be cached
    as the singleton of the *new* registration. The next resolve runs the
    replacement provider. Historically this scenario was a use-after-free
    (registry reallocation under a live descriptor reference).
    """
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

    second = container.resolve(IService)
    assert isinstance(second, Replacement)
    assert container.resolve(IService) is second


def test_decorator_resolving_same_key_detects_cycle(container):
    """Cycle detection covers the decorator phase, not just autowiring.

    A decorator runs while its own key is still mid-resolution; if it resolves
    that same key, the container must report a circular dependency instead of
    recursing forever. This pins that the resolution stack stays pushed for
    the whole provider -> decorators -> validation pipeline.
    """
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
    """Re-entrant resolution of a *different* key is legitimate and supported.

    Decorators commonly enrich an instance with other services (loggers,
    metrics). The cycle guard must be key-scoped: only re-entry on the same
    key is an error, so this cross-key lookup has to succeed.
    """
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
    """A class autowired with a type hint of itself is the smallest possible cycle.

    Before cycle detection existed this recursed until the native stack
    overflowed and the interpreter segfaulted. It must now surface as a
    plain, catchable RuntimeError.
    """
    container.register(_Selfish, _Selfish, autowire=True)

    with pytest.raises(RuntimeError, match="Circular dependency"):
        container.resolve(_Selfish)


# ---------------------------------------------------------------------------
# Key identity and naming edge cases
# ---------------------------------------------------------------------------


def test_empty_string_name_is_distinct_from_unnamed(container):
    """The empty string is a valid registration name, distinct from "no name".

    Keys are (interface, name|None); "" is a str like any other, so it must
    form its own key rather than collapsing into the unnamed registration.
    This guards the None-vs-"" boundary in the key policy's hashing and
    equality.
    """
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
    """Names round-trip through the C++ boundary without mangling.

    Registration names cross into std::string and back; multi-byte UTF-8
    (including astral-plane emoji) must survive both directions, and a
    near-miss name must still be a distinct key rather than a fuzzy match.
    """
    class IService:
        pass

    class Service(IService):
        pass

    container.register(IService, Service, name="サービス-🎁")

    assert isinstance(container.resolve((IService, "サービス-🎁")), IService)
    with pytest.raises(RuntimeError, match="No provider"):
        container.resolve((IService, "サービス"))


def test_tuple_key_with_none_name_equals_bare_interface(container):
    """resolve(I) and resolve((I, None)) are the same key, by contract.

    The tuple form exists for named lookups, but passing None as the name
    must normalize to the unnamed registration rather than creating a
    separate key space. Both lookup spellings and __contains__ must agree.
    """
    class IService:
        pass

    class Service(IService):
        pass

    container.register(IService, Service)

    assert (IService, None) in container
    assert isinstance(container.resolve((IService, None)), IService)


def test_identical_qualnames_are_distinct_keys(container):
    """Keying is by object identity, not by class name.

    Two classes created from the same factory share a __qualname__ but are
    different objects, so they must be independently registrable and
    resolvable. This pins the identity semantics against any future
    temptation to key by qualified name (which the registry module does,
    deliberately -- the two must not converge by accident).
    """
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
    """The container owns a strong reference to every registered interface.

    Keys store a borrowed PyObject*, so if the descriptor did not hold the
    interface object, dropping the last user reference would leave a dangling
    key (and id-reuse could silently alias a new class onto it). After the
    caller deletes its references, registered_keys() must still hand back a
    live, resolvable class.
    """
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
    """Error messages must cope with keys that have no __qualname__.

    The key-naming suffix prefers __qualname__ but must fall back to repr()
    for arbitrary objects (here an int instance) instead of raising a
    secondary error while formatting the primary one.
    """
    with pytest.raises(RuntimeError, match="No provider for key.*42"):
        container.resolve(42)


# ---------------------------------------------------------------------------
# Protocols, ABCs, and unusual providers
# ---------------------------------------------------------------------------


def test_runtime_checkable_protocol_as_interface(container):
    """Structural typing works: a runtime_checkable Protocol is a valid interface.

    The provider's class never inherits from the protocol; isinstance passes
    purely structurally (it has a send method). The negative half proves the
    check is real: a structurally non-conforming class fails validation at
    resolve time.
    """
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
    """A Protocol without @runtime_checkable fails with Python's own TypeError.

    isinstance() against such a protocol raises TypeError by design. The
    container must let that propagate unchanged -- neither swallowing it,
    nor rewrapping it as a RuntimeError -- so the user sees Python's
    actionable "add @runtime_checkable" message.
    """
    from typing import Protocol

    class Sender(Protocol):
        def send(self, message): ...

    class Smtp:
        def send(self, message):
            return message

    container.register(Sender, Smtp)

    with pytest.raises(TypeError):
        container.resolve(Sender)


def test_abc_virtual_subclass_passes_validation(container):
    """ABC virtual registration (Storage.register(Disk)) satisfies validation.

    The interface check is a plain isinstance(), so it must honor every hook
    Python offers -- including __subclasshook__ and abc virtual subclasses,
    where the provider's class has no real inheritance relationship with the
    interface.
    """
    import abc

    class Storage(abc.ABC):
        pass

    class Disk:  # not a real subclass
        pass

    Storage.register(Disk)
    container.register(Storage, Disk)

    assert isinstance(container.resolve(Storage), Storage)


def test_partial_and_bound_method_providers(container):
    """Providers only need to be callable: partials and bound methods qualify.

    The provider contract is "zero-argument callable returning an instance",
    not "class or function". functools.partial pre-binds constructor
    arguments, and a bound method carries its factory object along -- both
    common patterns that must keep working.
    """
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
    """Autowiring resolves type hints against *unnamed* registrations only.

    A dependency registered solely under a name is invisible to autowiring,
    so a typed parameter with a default falls back to that default. This is
    a documented limitation (there is no Annotated-style qualifier yet); the
    test keeps the behavior deliberate rather than accidental.
    """
    class Repo:
        pass

    class NamedRepo(Repo):
        pass

    class Service:
        def __init__(self, repo: Repo = None):
            self.repo = repo

    container.register(Repo, NamedRepo, name="only-named")
    container.register(Service, Service, autowire=True)

    assert container.resolve(Service).repo is None


def test_autowire_optional_annotation_falls_back_to_default(container):
    """Optional[X] does not match a registration for X.

    Optional[Repo] is a typing.Union object, and keys compare by identity,
    so the union never matches the registered class; with a default present
    the parameter falls back to it. Documents current behavior -- if union
    unwrapping is ever added, this test should be revisited consciously.
    Note: on Python <= 3.10, get_type_hints() also rewrites `x: X = None`
    into Optional[X] (implicit Optional), so such parameters behave this
    way there even without an explicit Optional annotation.
    """
    from typing import Optional

    class Repo:
        pass

    class Service:
        def __init__(self, repo: Optional[Repo] = None):
            self.repo = repo

    container.register(Repo, Repo)
    container.register(Service, Service, autowire=True)

    assert container.resolve(Service).repo is None


def test_error_chain_names_dependency_and_resolving_frame(container):
    """Failure messages identify both the missing parameter and the outer key.

    Each resolve frame appends its key to errors bubbling through it, so a
    failed autowired dependency reads as a chain: the parameter that could
    not be satisfied ('repo') and the service being resolved ('Service').
    Without both, diagnosing wiring mistakes in a deep graph means guesswork.
    """
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
    """A diamond (A -> B,C; B,C -> D) is not a cycle and shares the singleton.

    Cycle detection must trigger only on re-entry of a key that is still
    mid-resolution -- D is resolved twice on *sibling* paths, which is legal.
    Both arms must also receive the same D instance, proving the singleton
    cache works under re-entrant autowired resolution.
    """
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
    """A 40-level dependency chain resolves without exhausting any stack.

    Autowired resolution recurses natively (C++ -> Python -> C++ per level),
    so this guards against both stack exhaustion at realistic depths and
    off-by-one drift in the chain wiring itself: the resolved graph is
    walked and must contain exactly depth-1 links.
    """
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
    """A constructor with 32 autowired parameters wires every one correctly.

    Wide graphs exercise the ParamSpec extraction loop, kwargs assembly, and
    per-parameter resolution in bulk. Each parameter must receive an instance
    of exactly its annotated class -- any ordering or aliasing bug in the
    injection loop would scramble the pairing.
    """
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
    """Bulk usage: 2000 registrations stay consistent and individually resolvable.

    Registration growth stresses the index map, the capacity pre-reservation
    path, and registered_keys() enumeration. Spot-check resolves across the
    range prove that internal storage growth (vector reallocation, rehashing)
    never detached keys from their descriptors.
    """
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
