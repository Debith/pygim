"""
Basic usage example for gimdict.

gimdict is a high-performance dictionary implementation with C++ backing
that implements the full MutableMapping interface.
"""

from collections.abc import Mapping, MutableMapping
from pygim import gimdict


def main():
    print("=== Module Attributes ===")
    print(f"Available backends: {gimdict.backends}")
    print(f"Default backend: {gimdict.default_map}")
    print()
    
    print("=== Creating a gimdict ===")
    # Create a new gimdict instance
    my_map = gimdict()
    print(f"Empty gimdict: {my_map}")
    print(f"Is MutableMapping: {isinstance(my_map, MutableMapping)}")
    print()
    
    print("=== Basic Operations ===")
    # Set some values
    my_map['name'] = 'John'
    my_map['age'] = 30
    my_map['city'] = 'New York'
    
    print(f"Dictionary: {my_map}")
    print(f"Number of items: {len(my_map)}")
    print()
    
    print("=== Getting Values ===")
    print(f"Name: {my_map['name']}")
    print(f"Age: {my_map['age']}")
    print(f"Country (with default): {my_map.get('country', 'Unknown')}")
    print()
    
    print("=== Checking Keys ===")
    print(f"'name' in dictionary: {'name' in my_map}")
    print(f"'country' in dictionary: {'country' in my_map}")
    print()
    
    print("=== Update with |= Operator ===")
    my_map['key3'] = 3
    my_map |= dict(key1=1, key2=2)
    print(f"After |= update: {my_map}")
    print()
    
    print("=== Iteration ===")
    print(f"Keys: {list(my_map)}")
    print(f"Keys method: {my_map.keys()}")
    print(f"Values: {my_map.values()}")
    print(f"Items: {my_map.items()}")
    print()
    
    print("=== Advanced Methods ===")
    # setdefault
    default_val = my_map.setdefault('new_key', 'default_value')
    print(f"setdefault result: {default_val}")
    
    # pop
    popped = my_map.pop('key1', None)
    print(f"Popped key1: {popped}")
    
    # Clear the dictionary
    my_map.clear()
    print(f"After clear: {my_map}")


def comparison_example():
    """Example showing gimdict behaves like Python dict."""
    print("\n=== Comparison with Python dict ===")
    
    gd = gimdict()
    pd = {}
    
    # Both support the same operations
    gd['a'] = 1
    pd['a'] = 1
    
    gd.update({'b': 2, 'c': 3})
    pd.update({'b': 2, 'c': 3})
    
    print(f"gimdict keys: {set(gd)}")
    print(f"dict keys: {set(pd)}")
    print(f"Keys match: {set(gd) == set(pd)}")


if __name__ == '__main__':
    main()
    comparison_example()
