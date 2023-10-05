# -*- coding: utf-8 -*-
"""

"""

from _pygim._iterlib import flatten
from _pygim._magic._dispatcher import _Dispatcher
from _pygim._error_msgs import type_error_msg

_dispatch = _Dispatcher

__all__ = ["StringList"]

def _sep_flatten(iterable, sep):
    for item in flatten(iterable):
        yield from item.split(sep)


class StringListMeta(type):
    def __new__(mcls, name, bases=(), attrs=None, **kwargs):
        class _StringListBase:
            def __init__(self, *strings, sep=None, encoding='utf-8'):
                self._sep = sep or self.__class__._sep
                self._parts = list(_sep_flatten(strings, self._sep))
                self._encoding = encoding

            def __init_subclass__(cls, **kwargs) -> None:
                cls._sep = kwargs.pop('sep', cls._sep)

            def copy(self):
                return self.__class__(self._parts, sep=self._sep)

            def __repr__(self):
                return f"<{self.__class__.__name__}:{str(self).replace('\n', '\\n')}>"

            def __str__(self):
                return self._sep.join(self._parts)

            @_dispatch
            def __iadd__(self, *others):
                if not others:
                    raise NotImplementedError("Must provide at least one argument to add")

                if not isinstance(others[0], _StringListBase):
                    raise TypeError(type_error_msg(self.__class__.__name__, others[0]))

                self._parts.extend(others[0]._parts)
                return self

            @__iadd__.register(str)
            def _(self, other):
                self._parts.append(str(other))
                return self

            @__iadd__.register(bytes)
            def _(self, other):
                self._parts.append(other.decode(self._encoding))
                return self

            @__iadd__.register(list)
            @__iadd__.register(tuple)
            def _(self, other):
                for item in other:
                    self += item
                return self

            __add__ = __iadd__

            def __eq__(self, other):
                if not isinstance(other, self.__class__):
                    return False
                return other._parts == self._parts and other._sep == other._sep

        attrs = attrs or {}
        attrs = dict(_StringListBase.__dict__)
        attrs['_sep'] = kwargs.pop('sep', '\n')
        new_type = type(name, bases, attrs)
        return new_type


class StringList(metaclass=StringListMeta):
    """
    A base class for string list.
    """

def lines(*strings, sep='\n', encoding='utf-8'):
    return str(StringList(*strings, sep=sep, encoding=encoding))