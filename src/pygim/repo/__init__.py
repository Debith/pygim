"""Deprecated repository helpers.

The prior Python-only builder has been removed in favour of constructing
``pygim.repository_v2.Repository`` instances directly. This module is intentionally
empty and retained solely so that ``pygim.repo`` remains importable while users
transition away from the old APIs.
"""

__all__: list[str] = []
