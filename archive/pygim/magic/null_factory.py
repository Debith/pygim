"""
This module implements a pattern that can be used to create classes with null object counterpart.

This approach effectively combines factory and null patterns while taking advantage of
Python's powerful metaprogramming capabilities.
"""


class NullFactory(type):
    pass

