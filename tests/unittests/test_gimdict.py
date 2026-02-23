import pytest
from collections.abc import MutableMapping


def test_gimdict_module_attributes():
    """Test that gimdict module has required attributes."""
    from pygim import gimdict
    
    # Check module attributes
    assert hasattr(gimdict, 'backends')
    assert hasattr(gimdict, 'default_map')
    
    # Check backends tuple
    backends = gimdict.backends
    assert isinstance(backends, tuple)
    assert 'absl::flat_hash_map' in backends
    assert 'tsl::robin_map' in backends
    
    # Check default map
    assert gimdict.default_map == 'tsl::robin_map'


def test_gimdict_import():
    """Test that gimdict can be used directly."""
    from pygim import gimdict
    
    # Can instantiate directly
    d = gimdict()
    assert isinstance(d, MutableMapping)


def test_gimdict_vs_dict_basic_operations():
    """Test basic operations comparing gimdict with Python dict."""
    from pygim import gimdict
    
    gd = gimdict()
    pd = {}
    
    # Test set and get
    gd['key1'] = 'value1'
    pd['key1'] = 'value1'
    assert gd['key1'] == pd['key1']
    
    # Test contains
    assert ('key1' in gd) == ('key1' in pd)
    assert ('key2' in gd) == ('key2' in pd)
    
    # Test len
    assert len(gd) == len(pd)
    
    gd['key2'] = 'value2'
    pd['key2'] = 'value2'
    assert len(gd) == len(pd)


def test_gimdict_vs_dict_iteration():
    """Test iteration comparing gimdict with Python dict."""
    from pygim import gimdict
    
    gd = gimdict()
    pd = {}
    
    # Add same items
    items = {'key1': 1, 'key2': 2, 'key3': 3}
    for k, v in items.items():
        gd[k] = v
        pd[k] = v
    
    # Test iteration over keys
    gd_keys = set(gd)
    pd_keys = set(pd)
    assert gd_keys == pd_keys
    
    # Test list conversion
    assert set(list(gd)) == set(list(pd))


def test_gimdict_vs_dict_ior_operator():
    """Test |= operator comparing gimdict with Python dict."""
    from pygim import gimdict
    
    gd = gimdict()
    pd = {}
    
    gd['key3'] = 3
    pd['key3'] = 3
    
    update_dict = {'key1': 1, 'key2': 2}
    gd |= update_dict
    pd |= update_dict
    
    # Check all keys are present
    assert set(gd) == set(pd)
    assert gd['key1'] == pd['key1']
    assert gd['key2'] == pd['key2']
    assert gd['key3'] == pd['key3']


def test_gimdict_vs_dict_get():
    """Test get method comparing gimdict with Python dict."""
    from pygim import gimdict
    
    gd = gimdict()
    pd = {}
    
    gd['key'] = 'value'
    pd['key'] = 'value'
    
    # Get existing key
    assert gd.get('key') == pd.get('key')
    
    # Get non-existing key with no default
    assert gd.get('missing') == pd.get('missing')
    
    # Get non-existing key with default
    assert gd.get('missing', 'default') == pd.get('missing', 'default')


def test_gimdict_vs_dict_pop():
    """Test pop method comparing gimdict with Python dict."""
    from pygim import gimdict
    
    gd = gimdict()
    pd = {}
    
    gd['key1'] = 'value1'
    pd['key1'] = 'value1'
    
    # Pop existing key
    assert gd.pop('key1') == pd.pop('key1')
    assert len(gd) == len(pd)
    
    # Pop non-existing key with default
    gd['key2'] = 'value2'
    pd['key2'] = 'value2'
    assert gd.pop('missing', 'default') == pd.pop('missing', 'default')
    
    # Pop non-existing key without default should raise KeyError
    with pytest.raises(KeyError):
        gd.pop('nonexistent')
    with pytest.raises(KeyError):
        pd.pop('nonexistent')


def test_gimdict_vs_dict_popitem():
    """Test popitem method comparing gimdict with Python dict."""
    from pygim import gimdict
    
    gd = gimdict()
    pd = {}
    
    # Test empty dict
    with pytest.raises(KeyError):
        gd.popitem()
    with pytest.raises(KeyError):
        pd.popitem()
    
    # Add items and pop
    gd['key1'] = 'value1'
    pd['key1'] = 'value1'
    
    gd_item = gd.popitem()
    pd_item = pd.popitem()
    
    # Should return tuples
    assert isinstance(gd_item, tuple)
    assert isinstance(pd_item, tuple)
    assert len(gd_item) == 2
    
    # Should be empty after pop
    assert len(gd) == len(pd) == 0


def test_gimdict_vs_dict_setdefault():
    """Test setdefault method comparing gimdict with Python dict."""
    from pygim import gimdict
    
    gd = gimdict()
    pd = {}
    
    # Set default for non-existing key
    assert gd.setdefault('key1', 'default1') == pd.setdefault('key1', 'default1')
    assert gd['key1'] == pd['key1']
    
    # Set default for existing key (should return existing value)
    assert gd.setdefault('key1', 'new_default') == pd.setdefault('key1', 'new_default')
    assert gd['key1'] == pd['key1'] == 'default1'


def test_gimdict_vs_dict_update():
    """Test update method comparing gimdict with Python dict."""
    from pygim import gimdict
    
    gd = gimdict()
    pd = {}
    
    gd.update({'key1': 1, 'key2': 2})
    pd.update({'key1': 1, 'key2': 2})
    
    assert set(gd) == set(pd)
    assert gd['key1'] == pd['key1']
    assert gd['key2'] == pd['key2']


def test_gimdict_vs_dict_clear():
    """Test clear method comparing gimdict with Python dict."""
    from pygim import gimdict
    
    gd = gimdict()
    pd = {}
    
    gd['key1'] = 1
    pd['key1'] = 1
    
    gd.clear()
    pd.clear()
    
    assert len(gd) == len(pd) == 0


def test_gimdict_vs_dict_keys_values_items():
    """Test keys, values, items methods comparing gimdict with Python dict."""
    from pygim import gimdict
    
    gd = gimdict()
    pd = {}
    
    items = {'key1': 1, 'key2': 2, 'key3': 3}
    gd.update(items)
    pd.update(items)
    
    # Test keys
    assert set(gd.keys()) == set(pd.keys())
    
    # Test values
    assert set(gd.values()) == set(pd.values())
    
    # Test items
    assert set(gd.items()) == set(pd.items())


def test_gimdict_vs_dict_delitem():
    """Test __delitem__ comparing gimdict with Python dict."""
    from pygim import gimdict
    
    gd = gimdict()
    pd = {}
    
    gd['key1'] = 'value1'
    pd['key1'] = 'value1'
    
    del gd['key1']
    del pd['key1']
    
    assert len(gd) == len(pd) == 0
    
    # Delete non-existing key should raise KeyError
    with pytest.raises(KeyError):
        del gd['nonexistent']
    with pytest.raises(KeyError):
        del pd['nonexistent']


def test_gimdict_vs_dict_equality():
    """Test equality comparison."""
    from pygim import gimdict
    
    gd1 = gimdict()
    gd2 = gimdict()
    
    gd1['key1'] = 1
    gd2['key1'] = 1
    
    assert gd1 == gd2
    
    gd2['key2'] = 2
    assert gd1 != gd2


def test_gimdict_repr():
    """Test the string representation."""
    from pygim import gimdict
    
    d = gimdict()
    repr_str = repr(d)
    assert 'gimdict' in repr_str
    assert '{}' in repr_str
    
    d['key'] = 'value'
    repr_str = repr(d)
    assert 'gimdict' in repr_str
    assert 'key' in repr_str


def test_gimdict_key_error():
    """Test that KeyError is raised for missing keys."""
    from pygim import gimdict
    
    d = gimdict()
    
    with pytest.raises(KeyError):
        _ = d['nonexistent']


def test_gimdict_mutable_mapping():
    """Test that gimdict is a MutableMapping."""
    from pygim import gimdict
    
    d = gimdict()
    assert isinstance(d, MutableMapping)
