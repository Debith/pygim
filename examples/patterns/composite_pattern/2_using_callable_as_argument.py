# -*- coding: utf-8 -*-
"""
Example of composite pattern use in practice, using generated classes directly
while passing function as an argument for the pattern.
"""

from pygim.magic.composite_pattern import generate_composite_pattern_classes

# Generate three different classes by calling the function with default arguments.
ExComponent, ExLeaf, ExComposite = generate_composite_pattern_classes("ExCall", operation_name="execute")

# The `Component` class can be used directly to create correct object types.
# Here we pass simple function for each created components.
def execute(self, value):
    return self.__component * value

# Since the execute is the operation name, it is considered abstract in ExComponent
# base class. That means, you can add new function directly to each Leaf class
# through the ExComponent class.
ExComponent.execute = execute

example_tree = ExComponent([1, 2, 3, 4])
assert example_tree.execute(5) == [5, 10, 15, 20]

# To go extreme, you can add special handling for each leaf.
def leaf_execute():
    pass