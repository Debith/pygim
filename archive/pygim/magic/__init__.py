"""
This module contains collection of magical functions.
"""

def python_name_mangling(class_name, attr_name):
    return f"_{class_name.lstrip('_')}__{attr_name}"