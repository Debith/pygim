# -*- coding: utf-8 -*-
'''
Internal package for complaining functions.
'''

import types

__all__ = ['type_error_msg', 'TraitFunctions']

TraitFunctions = (types.FunctionType, types.MethodType)

def type_error_msg(obj, expected_type):
    if isinstance(expected_type, tuple):
        type_names = ",".join(f"`{t.__name__}`" for t in expected_type)
    else:
        type_names = type(obj).__name__
    return f"Expected to get type `{expected_type.__name__}`, got `{repr(obj)} [`{type_names}`]"