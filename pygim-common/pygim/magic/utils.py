# -*- coding: utf-8 -*-
"""
Magical utility functions.
"""

__all__ = ["anon_obj"]

def anon_obj():
    """ Create a quick dynamic object."""
    return type("__anonymous_obj__", (), {})()
