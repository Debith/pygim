"""

"""

from ..utils.iterable import flatten

def _sep_flatten(iterable, sep):
    for item in flatten(iterable):
        yield from item.split(sep)


class StringListMeta(type):
    class _StringListBase:
        def __init__(self, *strings, sep=None):
            self._sep = sep or self.__class__._sep
            self._parts = list(_sep_flatten(strings, self._sep))

        def __init_subclass__(cls, **kwargs) -> None:
            cls._sep = kwargs.pop('sep', cls._sep)

        def __repr__(self):
            return f"<{self.__class__.__name__}:{str(self)}>"

        def __str__(self):
            return self._sep.join(self._parts)

        def __add__(self, other):
            assert isinstance(other, (self.__class__, str))
            return self.__class__(self._parts + [str(other)])

        def __eq__(self, other):
            if not isinstance(other, self.__class__):
                return False
            return other._parts == self._parts and other._sep == other._sep

    def __new__(mcls, name, bases=(), attrs=None, **kwargs):
        attrs = attrs or {}
        attrs = dict(mcls._StringListBase.__dict__)
        attrs['_sep'] = kwargs.pop('sep', '\n')
        return type(name, bases, attrs)


class StringList(metaclass=StringListMeta):
    """
    A base class for string list.
    """