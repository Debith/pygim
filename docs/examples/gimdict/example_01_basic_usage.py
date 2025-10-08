"""
Basic usage example for GimDict.

GimDict is a high-performance dictionary implementation with C++ backing.
"""

from pygim import gimdict


def main():
    # Create a new GimDict instance
    d = gimdict.GimDict()
    
    # Set some values
    d['name'] = 'John'
    d['age'] = 30
    d['city'] = 'New York'
    
    print(f"Dictionary: {d}")
    print(f"Number of items: {len(d)}")
    
    # Get values
    print(f"Name: {d['name']}")
    print(f"Age: {d['age']}")
    
    # Check if keys exist
    print(f"'name' in dictionary: {'name' in d}")
    print(f"'country' in dictionary: {'country' in d}")
    
    # Clear the dictionary
    d.clear()
    print(f"After clear: {d}")


if __name__ == '__main__':
    main()
