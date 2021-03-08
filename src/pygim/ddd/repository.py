"""
This module implements DDD repository using Pythonic open-closed principle.
"""


from collections.abc import Mapping

from ..system import Factory
import attr


class Repository(Mapping):
    pass


ddd_factory = Factory(__name__)
