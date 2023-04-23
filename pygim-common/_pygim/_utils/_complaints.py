# -*- coding: utf-8 -*-
'''
Internal package for complaining functions.
'''

__all__ = ['complain_type']

def complain_type(obj, expected_type):
    return f"Expected to get type `{expected_type.__name__}`, got `{repr(obj)} [`{type(obj).__name__}`]"