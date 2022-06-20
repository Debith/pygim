# -*- coding: utf-8 -*-
"""
Contains error message with multiline errors.
"""

from dataclasses import dataclass, field
from pygim.utils import flatten
from pygim import typing as t

T = t.TypeVar('T', bound='GimError')


@dataclass
class GimError(Exception):
    """ Simple error class for handling multiple error messages at once.

    It works with single string instance:
        >>> raise GimError("It fails")
        Traceback (most recent call last):
        ...
        GimError: It fails

    It is possible to add new messages later on:
        >>> err = GimError("You should:")
        >>> bool(err)
        False
        >>> err.add("behave nicely")
        >>> err.add("smile happily")
        >>> bool(err)
        True
        >>> raise err
        Traceback (most recent call last):
        ...
        GimError: You should:
          - behave nicely
          - smile happily

    You can use operand too:
        >>> raise GimError("Error works:") << "nicely" << "beautifully"
        Traceback (most recent call last):
        ...
        GimError: Error works:
          - nicely
          - beautifully
    """
    _initial_msg: str
    _errors: list = field(default_factory=list)

    def __str__(self: T) -> t.Text:
        msgs = [self._initial_msg]
        msgs += [f"  - {str(v)}" for v in self._errors]
        return "\n".join(msgs)

    def add(self: T, *sub_msgs: t.NestedIterable[t.SupportsStr]) -> None:
        assert all([isinstance(s, t.SupportsStr) for s in flatten(sub_msgs)])
        self._errors.extend(flatten(sub_msgs))

    def clear(self: T) -> None:
        self._errors = []

    def __lshift__(self: T, *sub_msgs: t.NestedIterable[t.SupportsStr]) -> T:
        self.add(sub_msgs)  # type: ignore  # TODO: Fix this later
        return self

    def __bool__(self: T) -> bool:
        return bool(self._errors)


if __name__ == "__main__":
    import doctest
    doctest.testmod()