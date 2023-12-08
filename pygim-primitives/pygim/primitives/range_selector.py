# -*- coding: utf-8 -*-
'''
This module implmements class RangeSelector.
'''

from collections.abc import Mapping, Iterable
from types import MappingProxyType
from dataclasses import dataclass, field
from tabulate import tabulate
from pygim.gimmicks import gimmick, gim_type
from pygim.explib import GimError
from pygim.checklib import has_instances

__all__ = ['RangeSelector']


class RangeSelectorMeta(gim_type):
    ''' Metaclass for RangeSelector.

    '''
    def __call__(cls, ranges, values=None):
        if not ranges:
            raise GimError('Ranges must be specified')
        if isinstance(ranges, Mapping):
            ranges = sorted(ranges.items(), key=lambda x: x[0][0])
            assert not values, 'Values must not be specified'
            # Ranges must be consecutive
            for i in range(len(ranges) - 1):
                left = ranges[i][0][1]
                right = ranges[i + 1][0][0]
                if left != right:
                    raise GimError('Ranges must be consecutive')
            ranges = dict(ranges)

        else:
            if has_instances(ranges, Iterable):
                ranges = list(ranges)
            elif has_instances(ranges, int):
                ranges = list(zip(ranges[:-1], ranges[1:]))
            else:
                raise GimError('Ranges must be a list of tuples or integers')

            ranges = sorted(ranges, key=lambda x: x[0])

            # Ranges must be consecutive
            for i in range(len(ranges) - 1):
                left = ranges[i][1]
                right = ranges[i + 1][0]
                if left != right:
                    raise GimError('Ranges must be consecutive')

            if values:
                if len(ranges) != len(values):
                    raise GimError('Number of ranges and values must be equal')
            else:
                values = range(len(ranges))
            ranges = dict(zip(ranges, values))

        return super().__call__(MappingProxyType(ranges))


@dataclass(frozen=True, slots=True)
class RangeSelector(gimmick, metaclass=RangeSelectorMeta):
    ''' RangeSelector class.

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

    '''
    _ranges: Mapping

    def __post_init__(self):
        assert self._ranges, 'Ranges must be specified'
        assert has_instances(self._ranges, tuple), 'keys must be tuples'

    def __getitem__(self, index):
        ''' Returns the range at the given index.
        
        Parameters
        ----------
        index : int | tuple
            The index of the range to return. If a tuple is given, it is
            used to select the range.
        
        Returns
        -------
        tuple
            The range at the given index.
        '''
        if isinstance(index, tuple):
            return self._ranges[index]
        return list(self._ranges.keys())[index]

    def __len__(self):
        ''' Returns the number of ranges.'''
        return len(self._ranges)

    def __iter__(self):
        ''' Returns an iterator over the ranges.'''
        yield from self._ranges.keys()

    def __reversed__(self):
        ''' Returns a reversed iterator over the ranges.'''
        yield from reversed(self._ranges.keys())

    def __repr__(self):
        ''' Returns a string representation of the ranges.'''
        keys = list(self._ranges.keys())
        values = list(self._ranges.values())
        if has_instances(values, int):
            args = [keys]
        else:
            args = [keys, values]
        
        return f'RangeSelector({", ".join(repr(a) for a in args)})'

    def __str__(self):
        ''' Returns a string representation of the ranges and their values.'''
        table = [(k[0], '->', k[1], ':', v) for k, v in self._ranges.items()]
        return tabulate(table, tablefmt="plain")

    def __contains__(self, value):
        ''' Checks if the given value is in any of the ranges.

        Parameters
        ----------
        value : int
            The value to check.

        Returns
        -------
        bool
            True if the value is in any of the ranges, False otherwise.
        '''
        first = list(self._ranges.keys())[0][0]
        last = list(self._ranges.keys())[-1][-1]
        return first <= value < last

    def select(self, value):
        ''' Selects the range for the given value.

        Parameters
        ----------
        value : int
            The value to select the range for.

        Returns
        -------
        int
            The index of the range that contains the given value.
        '''
        for (lower, upper), content in self._ranges.items():
            if lower <= value < upper:
                return content
        raise GimError('Value is out of range')


if __name__ == '__main__':
    import doctest
    doctest.testmod()