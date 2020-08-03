"""
This module contains all the exceptions used within this library.
"""

class PythonGimmicksException(Exception):
    """ Base class for exceptions used in this library. """



class FactoryMethodNotFoundException(PythonGimmicksException):
    def __init__(self, available_attributes, name):
        factory_methods, objects = split(available_attributes, lambda aa: aa.startswith('create'))

        if name.startswith('create'):
            kind = "factory methods"
            options = ', '.join(factory_methods)
        else:
            kind = "objects"
            options = ', '.join(objects)

        if not options:
            msg = f"When attempting to access '{name}', list of {kind} is EMPTY!"
        else:
            msg = f"Unable to find '{name}' among from available {kind}: {options}"

        super().__init__(msg)


class FactoryMethodRegisterationException(PythonGimmicksException):
    """ This exception is raised, when any issue is detected when trying to register factory method. """