"""
This module holds logic for exceptions.
"""

from dataclasses import dataclass
import _pygim.typing as t

@dataclass
class GimException(Exception):
    """ Generic exception that can be used across Python projects. """
    _msg: t.Union[t.Text, t.Iterable[t.Text]] = ""
    _sep: t.Text = '\n'

    def __post_init__(self) -> None:
        assert isinstance(self._msg, (t.Text, t.Iterable))
        assert isinstance(self._sep, (t.Text))

    def __str__(self) -> t.Text:
        if isinstance(self._msg, str):
            return self._msg

        return self._sep.join(str(m) for m in self._msg)


class GimError(GimException):
    """ Error used as a base class for all exception in this project. """