# -*- coding: utf-8 -*-
"""
This module implmements class RangeSelector.
"""

from collections.abc import Mapping, Iterable, Sequence
from types import MappingProxyType
from dataclasses import dataclass
from tabulate import tabulate
from pygim.gimmicks import gimmick, gim_type
from pygim.explib import GimError, GimOptionError, type_error_msg
from pygim.checklib import has_instances
from pygim.performance import dispatch, UnrecognizedTypeError

__all__ = ["RangeSelector"]

EXCEPTION = object()

class RangeSelectorMeta(gim_type):
    """Metaclass as factory for RangeSelector."""

    _NON_EMPTY_MSG = "Parameter ``ranges`` must be specified."
    _MAPPING_OR_SEQUENCE_MSG = "Parameter ``ranges`` must be a mapping or a sequence."
    _TUPLES_SIZE_TWO_MSG = "Tuples in ``ranges`` must have length 2."

    def _is_consequtive(self, ranges: Mapping):
        """Checks if the given ranges are consecutive.

        Parameters
        ----------
        ranges : list
            The ranges to check.

        Returns
        -------
        bool
            True if the ranges are consecutive, False otherwise.
        """
        keys = list(ranges.keys())
        for i in range(len(keys) - 1):
            left = keys[i][1]
            right = keys[i + 1][0]
            if left != right:
                # This branch is only taken if the ranges are not consecutive,
                # which is used only in assertion.
                return False  # pragma: no cover
        return True

    @dispatch
    def __call__(cls, ranges, values=None):
        """Creates a new RangeSelector object."""
        if not ranges:
            raise GimError(cls._NON_EMPTY_MSG)
        raise UnrecognizedTypeError(ranges, (Mapping, Sequence))

    @__call__.register(dict)
    def _(cls, ranges: Mapping, *_):
        assert ranges, cls._NON_EMPTY_MSG

        ranges = dict(sorted(ranges.items(), key=lambda x: x[0][0]))

        # Ranges must be consecutive
        assert cls._is_consequtive(ranges), "Ranges must be consecutive"

        return super().__call__(MappingProxyType(ranges))

    @__call__.register(list)
    def _(cls, ranges: Sequence, labels=None):
        assert ranges, cls._NON_EMPTY_MSG

        if has_instances(ranges, Iterable):
            assert all(has_instances(r, int) for r in ranges), cls._MAPPING_OR_SEQUENCE_MSG
            assert all(len(r) == 2 for r in ranges), cls._TUPLES_SIZE_TWO_MSG
            ranges = list(ranges)
        elif has_instances(ranges, int):
            ranges = list(zip(ranges[:-1], ranges[1:]))

        if labels:
            assert len(ranges) == len(labels), "Number of ranges and labels must be equal"
            ranges = dict(zip(ranges, labels))
        else:
            ranges = dict(zip(ranges, range(len(ranges))))

        # Ranges is now a dictionary, so we can use the mapping version of the function
        return cls.__call__(ranges)


@dataclass(frozen=True, slots=True)
class RangeSelector(gimmick, metaclass=RangeSelectorMeta):
    """RangeSelector class.

    This class implements a range selector. It is used to select a range
    based on a value. The ranges are defined as a list of tuples. The
    first element of the tuple is the lower bound of the range and the
    second element is the upper bound of the range. The ranges must be
    sorted in ascending order (which is ensured). The ranges are inclusive
    on the lower bound and exclusive on the upper bound.

    Example
    -------
    >>> rs = RangeSelector([
    ...    (-10, 0),
    ...    (0, 10),
    ...    (10, 20),
    ...    (20, 30),
    ...    (30, 40),
    ... ])
    >>> rs.select(5)
    1
    >>> rs[0]
    (-10, 0)

    With values:
    >>> rs = RangeSelector([-10, 0, 10], ['a', 'b'])
    >>> rs.select(2)
    'b'
    >>> rs[0]
    (-10, 0)

    With a dictionary:
    >>> rs = RangeSelector({
    ...    (-10, 0): 'a',
    ...    (0, 10): 'b',
    ...    (10, 20): 'c',
    ...    (20, 30): 'd',
    ...    (30, 40): 'e',
    ... })
    >>> rs.select(5)
    'b'
    >>> rs[0]
    (-10, 0)

    """

    _ranges: Mapping

    def __post_init__(self):
        assert self._ranges, "Ranges must be specified"
        assert has_instances(self._ranges, tuple), "keys must be tuples"

    def _find_first_range_index(self, start):
        ranges = list(self._ranges.keys())
        low, high = 0, len(ranges) - 1
        while low <= high:
            mid = (low + high) // 2
            if ranges[mid][1] <= start:
                low = mid + 1
            else:
                high = mid - 1
        return ranges[low] if low < len(ranges) else None

    def _find_last_range_index(self, stop):
        ranges = list(self._ranges.keys())
        low, high = 0, len(ranges) - 1
        while low <= high:
            mid = (low + high) // 2
            if ranges[mid][0] >= stop:
                high = mid - 1
            else:
                low = mid + 1
        return ranges[high] if high >= 0 else None

    def _get_ranges_in_slice(self, index_slice):
        first_range = self._find_first_range_index(index_slice.start)
        last_range = self._find_last_range_index(index_slice.stop)

        if not first_range or not last_range:
            return []  # No valid ranges found

        ranges = list(self._ranges.keys())
        first_index = ranges.index(first_range)
        last_index = ranges.index(last_range)

        return [self._ranges[r] for r in ranges[first_index:last_index + 1]]

    def __getitem__(self, index):
        """
        Retrieves a specific range or a list of ranges based on the provided index.

        This method can handle three types of inputs:
        - An integer, which will return the label of the range containing this value.
        - A tuple, which directly indexes into the underlying range dictionary.
        - A slice, which returns a list of labels for all ranges that intersect with the slice.

        Parameters
        ----------
        index : int | tuple | slice
            The index used to retrieve the range(s).
            - If an int, it is the value for which the containing range's label is returned.
            - If a tuple, it directly indexes the range dictionary.
            - If a slice, it specifies the start and stop values to retrieve a list of range labels.

        Returns
        -------
        str | tuple | list
            Depending on the type of `index`:
            - If `index` is an int, returns the label of the range containing the value.
            - If `index` is a tuple, returns the range associated with the tuple key.
            - If `index` is a slice, returns a list of labels of ranges intersecting with the slice.

        Raises
        ------
        GimError
            If an int index does not fall within any defined range.
        """
        assert isinstance(index, (int, tuple, slice)), type_error_msg(index, (str, tuple))

        if isinstance(index, tuple):
            return self._ranges[index]
        elif isinstance(index, slice):
            return self._get_ranges_in_slice(index)
        return self.select(index)

    def __len__(self):
        """Returns the number of ranges."""
        return len(self._ranges)

    def __iter__(self):
        """Returns an iterator over the ranges."""
        yield from self._ranges.keys()

    def __reversed__(self):
        """Returns a reversed iterator over the ranges."""
        yield from reversed(self._ranges.keys())

    def __repr__(self):
        """Returns a string representation of the ranges."""
        keys = list(self._ranges.keys())
        values = list(self._ranges.values())
        if has_instances(values, int):
            args = [keys]
        else:
            args = [keys, values]

        return f'RangeSelector({", ".join(repr(a) for a in args)})'

    def __str__(self):
        """Returns a string representation of the ranges and their values."""
        table = [(k[0], "->", k[1], ":", v) for k, v in self._ranges.items()]
        return tabulate(table, tablefmt="plain")

    def __contains__(self, value):
        """Checks if the given value is in any of the ranges.

        Parameters
        ----------
        value : int
            The value to check.

        Returns
        -------
        bool
            True if the value is in any of the ranges, False otherwise.
        """
        return self.start <= value < self.end

    @property
    def start(self):
        """Returns the start of the range."""
        return list(self._ranges.keys())[0][0]

    @property
    def end(self):
        """Returns the end of the range."""
        return list(self._ranges.keys())[-1][-1]

    def find(self, value):
        for _range, _content in self._ranges.items():
            if value == _content:
                return _range
        raise GimOptionError(_content, self._ranges.values())

    def select(self, input_value, *, default=EXCEPTION):
        ''' Match input value for range and get its content.

        Parameters
        ----------
        value : int
            The value to select the range for.

        Returns
        -------
        int
            The index of the range that contains the given value.
        '''
        try:
            range_key = self._find_first_range_index(input_value)
            if range_key and range_key[0] <= input_value < range_key[1]:
                return self._ranges[range_key]
            else:
                raise KeyError
        except KeyError:
            if default is not EXCEPTION:
                return default
            emsg = f'Value {input_value} is out of range of {self.start}-{self.end}'
            raise GimError(emsg) from None


if __name__ == "__main__":
    import doctest

    doctest.testmod()
