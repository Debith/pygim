# gimdict - High-Performance Dictionary

## Overview

`gimdict` is a high-performance dictionary implementation with C++ backing that fully implements Python's `MutableMapping` interface. It provides the same API as Python's built-in `dict` while leveraging C++ hash maps for improved performance.

## Features

- **Full MutableMapping Interface**: Compatible with `collections.abc.MutableMapping`
- **Multiple Backend Support**: Designed to support multiple hash map implementations
  - `absl::flat_hash_map`
  - `tsl::robin_map` (default)
- **Python dict API**: All standard dictionary operations are supported
- **Performance**: C++-backed implementation for high-speed operations

## Module Attributes

```python
from pygim import gimdict

# Available backends
print(gimdict.backends)  # ('absl::flat_hash_map', 'tsl::robin_map')

# Default backend
print(gimdict.default_map)  # 'tsl::robin_map'
```

## Basic Usage

### Creating a gimdict

```python
from pygim import gimdict

# Create an empty gimdict
d = gimdict()

# Verify it's a MutableMapping
from collections.abc import MutableMapping
assert isinstance(d, MutableMapping)  # True
```

### Setting and Getting Values

```python
# Set values
d['key1'] = 'value1'
d['key2'] = 'value2'

# Get values
print(d['key1'])  # 'value1'

# Get with default
print(d.get('key3', 'default'))  # 'default'
```

### Checking Keys

```python
# Check if key exists
if 'key1' in d:
    print("Key exists!")

# Get length
print(len(d))  # 2
```

### Iteration

```python
# Iterate over keys
for key in d:
    print(key)

# Get keys, values, items
print(list(d.keys()))    # ['key1', 'key2']
print(list(d.values()))  # ['value1', 'value2']
print(list(d.items()))   # [('key1', 'value1'), ('key2', 'value2')]
```

### Update Operations

```python
# Update with |= operator
d |= {'key3': 'value3', 'key4': 'value4'}

# Update method
d.update({'key5': 'value5'})
```

### Removing Items

```python
# Delete item
del d['key1']

# Pop item
value = d.pop('key2')
value_with_default = d.pop('missing', 'default')

# Pop arbitrary item
key, value = d.popitem()

# Clear all items
d.clear()
```

### Other Methods

```python
# Set default if key doesn't exist
value = d.setdefault('new_key', 'default_value')
```

## Comparison with Python dict

`gimdict` behaves identically to Python's built-in `dict`:

```python
from pygim import gimdict

gd = gimdict()
pd = {}

# Same operations
gd['a'] = 1
pd['a'] = 1

gd |= {'b': 2}
pd |= {'b': 2}

# Same results
assert set(gd) == set(pd)
assert gd['a'] == pd['a']
```

## API Reference

### Constructor

- `gimdict()`: Create an empty gimdict

### Item Access

- `d[key]`: Get item (raises `KeyError` if not found)
- `d[key] = value`: Set item
- `del d[key]`: Delete item (raises `KeyError` if not found)

### Methods

- `get(key, default=None)`: Get item with optional default
- `pop(key, default=None)`: Remove and return item, with optional default
- `popitem()`: Remove and return arbitrary (key, value) pair
- `setdefault(key, default=None)`: Get value if exists, else set and return default
- `update(other)`: Update from another dict or mapping
- `clear()`: Remove all items
- `keys()`: Return list of keys
- `values()`: Return list of values
- `items()`: Return list of (key, value) tuples

### Operators

- `key in d`: Check if key exists
- `len(d)`: Get number of items
- `iter(d)`: Iterate over keys
- `d |= other`: Update from other dict (in-place OR)
- `d == other`: Check equality

### Special Methods

- `__repr__()`: String representation

## Performance Considerations

- Currently uses `std::unordered_map` as the underlying implementation
- Designed to support multiple backends (absl::flat_hash_map, tsl::robin_map)
- C++ backing provides performance benefits for large dictionaries
- All keys are currently strings

## Examples

See `example_01_basic_usage.py` for a comprehensive example demonstrating all features.
