import pytest
import sys

#pytest.importorskip("registry")
from pygim.registry import Registry

def test_register_and_getitem():
    reg = Registry()
    def foo(): return 42
    reg.register('foo', foo)
    assert 'foo' in reg
    assert callable(reg['foo'])
    assert reg['foo']() == 42
    assert len(reg) == 1

def test_register_override():
    reg = Registry()
    def foo(): return 1
    reg.register('foo', foo)
    def bar(): return 2
    with pytest.raises(RuntimeError):
        reg.register('foo', bar)
    reg.register('foo', bar, override=True)
    assert reg['foo']() == 2

def test_remove():
    reg = Registry()
    def foo(): return 1
    reg.register('foo', foo)
    del reg['foo']
    assert 'foo' not in reg
    del reg['nonexistent']  # Should not raise

def test_decorator():
    reg = Registry()
    @reg.register('foo')
    def foo():
        return 'bar'
    assert 'foo' in reg
    assert reg['foo']() == 'bar'

    # Test override with decorator
    with pytest.raises(RuntimeError):
        @reg.register('foo')
        def foo2():
            return 'baz'
    @reg.register('foo', override=True)
    def foo3():
        return 'baz'
    assert reg['foo']() == 'baz'

def test_missing():
    reg = Registry()
    with pytest.raises(RuntimeError):
        _ = reg['missing']


if __name__ == "__main__":
    from pygim.core.testing import run_tests
    run_tests(__file__, Registry.__module__, coverage=False)