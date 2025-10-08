import pytest


def test_gimdict_import():
    """Test that gimdict module can be imported."""
    from pygim import gimdict
    assert hasattr(gimdict, 'GimDict')


def test_gimdict_basic_operations():
    """Test basic dictionary operations."""
    from pygim import gimdict
    
    d = gimdict.GimDict()
    
    # Test set and get
    d['key1'] = 'value1'
    assert d['key1'] == 'value1'
    
    # Test contains
    assert 'key1' in d
    assert 'key2' not in d
    
    # Test len
    assert len(d) == 1
    
    d['key2'] = 'value2'
    assert len(d) == 2
    
    # Test clear
    d.clear()
    assert len(d) == 0


def test_gimdict_key_error():
    """Test that KeyError is raised for missing keys."""
    from pygim import gimdict
    
    d = gimdict.GimDict()
    
    with pytest.raises(KeyError):
        _ = d['nonexistent']


def test_gimdict_repr():
    """Test the string representation."""
    from pygim import gimdict
    
    d = gimdict.GimDict()
    repr_str = repr(d)
    assert 'GimDict' in repr_str
    assert '0 items' in repr_str
    
    d['key'] = 'value'
    repr_str = repr(d)
    assert '1 items' in repr_str
