"""
This module implements DDD repository using Pythonic open-closed principle.
"""

from collections.abc import Mapping

import attr


class Repository(Mapping):
    pass