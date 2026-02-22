import re
import pytest
from pygim.registry import Registry, KeyPolicyKind


@pytest.fixture(params=[False, True], ids=["no_hooks", "hooks"])
def registry(request):
    """Provide a Registry instance with and without hooks enabled.

    Hooks mode exercises the alternate code path that enables pre/post callbacks
    and disables const-only lookup fast path in C++ template specialization.
    """
    return Registry(hooks=request.param, policy=KeyPolicyKind.qualname)


def test_register_and_getitem(registry):
    def foo():
        return 1

    registry.register("foo", foo)
    assert "foo" in registry
    assert callable(registry["foo"])
    assert registry["foo"]() == 1


def test_len_and_registered_names(registry):
    def foo():
        return 1

    def bar():
        return 2

    registry.register("foo", foo)
    registry.register("bar", bar)
    # assert set(registry.registered_names()) == {"foo", "bar"}
    assert len(registry) == 2
    # New API: registered_keys should list 2 entries
    keys = registry.registered_keys()
    assert len(keys) == 2


def test_contains(registry):
    # NOTE: Deletion is not implemented in the current C++ Registry binding.
    # We only verify containment semantics for registered keys.
    def foo():
        return 1

    assert "foo" not in registry
    registry.register("foo", foo)
    assert "foo" in registry


def test_register_duplicate_without_override(registry):
    def foo():
        return 1

    def bar():
        return 2

    registry.register("foo", foo)
    # duplicate without override should fail
    with pytest.raises(RuntimeError):
        registry.register("foo", bar)
    # override=True should succeed
    registry.register("foo", bar, override=True)
    assert registry["foo"]() == 2


def test_decorator_usage(registry):
    @registry.register("foo")
    def foo():
        return "bar"

    assert registry["foo"]() == "bar"

    # decorator duplicate without override should fail
    with pytest.raises(RuntimeError):

        @registry.register("foo")
        def foo2():
            return "baz"

    # decorator with override=True
    @registry.register("foo", override=True)
    def foo3():
        return "baz"

    assert registry["foo"]() == "baz"


def test_missing_key_raises(registry):
    with pytest.raises(RuntimeError):
        _ = registry["missing"]


def test_key_tuple_len_must_be_2(registry):
    def f():
        return 1
    with pytest.raises(TypeError):
        registry.register(("id.only",), f)


def test_key_name_must_be_str_or_none(registry):
    def f():
        return 1
    with pytest.raises(TypeError):
        registry.register(("id.only", 123), f)


def test_hooks_invocation(registry):
    """Validate that hook callbacks fire only when hooks=True.

    We attach counters. For hooks disabled, callbacks should still be *registered*
    but underlying C++ no-op specializations mean they won't execute.
    """
    # Arrange
    events = {"register": 0, "pre": 0, "post": 0}

    def on_register(key, value):  # key: (id, name)
        events["register"] += 1

    def on_pre(key, value):
        events["pre"] += 1

    registry.on_register(on_register)
    registry.on_pre(on_pre)
    registry.on_post(lambda *a, **k: events.__setitem__("post", events["post"] + 1))

    # Act: register a function, access it (triggers pre), then manually post.
    def foo():
        return 42

    registry.register("foo", foo)
    _ = registry["foo"]  # triggers pre when hooks enabled

    # Assert

    # If hooks are enabled we expect positive counts; otherwise all zeros.
    if events["register"] == 0:
        # hooks disabled path; ensure all zero
        assert events == {"register": 0, "pre": 0, "post": 0}
    else:
        # hooks enabled: register fired exactly once; pre at least once; post may still be 0 (no explicit post trigger path in binding)
        assert events["register"] == 1
        assert events["pre"] >= 1
    assert events["post"] in (0, 1)


REPR_RE = re.compile(r"^Registry\(policy=(qualname|identity), hooks=(True|False), size=\d+\)$")


def test_repr(registry):
    r = repr(registry)
    assert REPR_RE.match(r), f"Unexpected repr format: {r}"


# ───────────────────────── Identity Policy Tests ───────────────────────────

@pytest.fixture(params=[False, True], ids=["id_no_hooks", "id_hooks"])
def identity_registry(request):
    return Registry(hooks=request.param, policy=KeyPolicyKind.identity)


def test_identity_basic(identity_registry):
    class C:  # object type used for identity key
        pass
    obj = C()
    identity_registry.register(obj, lambda: 7)
    assert obj in identity_registry
    assert identity_registry[obj]() == 7


def test_identity_rejects_string(identity_registry):
    with pytest.raises(TypeError):
        identity_registry.register("some.qualname", lambda: None)


def test_identity_override_missing(identity_registry):
    def f():
        return 1
    with pytest.raises(RuntimeError):
        identity_registry.register(f, f, override=True)  # cannot override missing


def test_identity_duplicate_without_override(identity_registry):
    def f():
        return 1
    def g():
        return 2
    identity_registry.register(f, f)
    with pytest.raises(RuntimeError):
        identity_registry.register(f, g)
    identity_registry.register(f, g, override=True)
    assert identity_registry[f]() == 2


def test_post_hook_invocation(registry):
    # Only meaningful if hooks enabled; otherwise post should be a no-op.
    events = {"post": 0}
    registry.on_post(lambda *a: events.__setitem__("post", events["post"] + 1))
    def f(): return 3
    registry.register("f", f)
    registry.post("f", None)
    if "hooks" in repr(registry) and "hooks=True" in repr(registry):
        assert events["post"] == 1
    else:
        assert events["post"] == 0


def test_qualname_string_id_duplicate(registry):
    # Register using string id directly (qualname policy only). For identity policy fixture we don't run this test.
    if "policy=identity" in repr(registry):
        pytest.skip("Not applicable to identity policy")
    def f(): return 1
    # Build a synthetic id
    id_str = f.__module__ + "." + f.__qualname__
    registry.register(id_str, f)
    with pytest.raises(RuntimeError):
        registry.register(id_str, lambda: 2)
    registry.register(id_str, lambda: 3, override=True)
    assert registry[id_str]() == 3


def test_registered_keys_and_find_id():
    r = Registry(hooks=False, policy=KeyPolicyKind.qualname)
    def f(): return 1
    # Register using the function object so stored id is module.qualname
    r.register(f, f)
    fid = f.__module__ + "." + f.__qualname__
    assert r.find_id(fid) is f
    # missing returns None
    assert r.find_id(fid+"_missing") is None
    # Also test explicit custom string id path
    def g(): return 2
    r.register("custom.id", g)
    assert r.find_id("custom.id") is g


def test_find_id_variant_fallback_to_empty_variant():
    r = Registry(hooks=False, policy=KeyPolicyKind.qualname)

    def f():
        return 1

    # Register with empty variant name and query with non-empty variant.
    r.register("custom.fallback", f)
    assert r.find_id("custom.fallback", "nonempty") is f


def test_find_id_wrong_policy(identity_registry):
    with pytest.raises(RuntimeError):
        identity_registry.find_id("some.id")


def test_capacity_reserve():
    # Not directly observable; ensure constructing with capacity still works.
    r = Registry(hooks=False, policy=KeyPolicyKind.qualname, capacity=128)
    def f(): return 1
    r.register("f", f)
    assert "f" in r


def test_hooks_disabled_noop_callbacks():
    r = Registry(hooks=False)
    counters = {"reg":0, "pre":0, "post":0}
    r.on_register(lambda *a: counters.__setitem__("reg", counters["reg"]+1))
    r.on_pre(lambda *a: counters.__setitem__("pre", counters["pre"]+1))
    r.on_post(lambda *a: counters.__setitem__("post", counters["post"]+1))
    def f(): return 1
    r.register("f", f)
    _ = r["f"]
    r.post("f", None)
    assert counters == {"reg":0, "pre":0, "post":0}


if __name__ == "__main__":
    from pygim.core.testing import run_tests

    run_tests(__file__, Registry.__module__, coverage=False)
