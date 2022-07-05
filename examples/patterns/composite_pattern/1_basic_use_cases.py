# -*- coding: utf-8 -*-
"""
Example of composite pattern use in practice, using generated classes directly
without inheriting from them.
"""

from pygim.magic.composite_pattern import generate_composite_pattern_classes

# Three different classes are generated.
ExComponent, ExLeaf, ExComposite = generate_composite_pattern_classes("Ex")

# The `Component` class can be used directly to create correct object types.
# It is useful if if you want to build initial tree of objects.
example_tree = ExComponent([1, 2, 3, 4])
assert example_tree == ExComposite([ExLeaf(1), ExLeaf(2), ExLeaf(3), ExLeaf(4)])

# In its simplest form, the composite pattern returns the data given to it
# in exactly same way it was given.
assert example_tree.execute() == [1, 2, 3, 4]

# Even with nested components, it is possible to get same data back.
another_tree = ExComponent([1, 2, [3, 4], 5, 6])
assert another_tree.execute() == [1, 2, [3, 4], 5, 6]

# Finally, composite object supports typical list operations as normal Python list
# does.
example_tree.insert(2, [2.25, 2.5, 2.75])
assert example_tree.execute() == [1, 2, [2.25, 2.5, 2.75], 3, 4]
assert example_tree[1] == ExLeaf(2)
assert example_tree[2][2] == ExLeaf(2.75)