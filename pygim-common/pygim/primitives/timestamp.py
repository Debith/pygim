# -*- coding: utf-8 -*-
"""
This module contains implementation of TimeStamp class.
"""

from enum import _EnumDict, Enum, EnumMeta
from datetime import datetime as pydt
from time import time_ns

from typing import Any
from pygim.gimmicks.abc import Interface, InterfaceMeta


class TimeUnitMeta(EnumMeta):
    def __new__(metacls, cls_name, bases, classdict, **kwds: Any):
        classdict["_aliases"] = {}
        cls = super().__new__(metacls, cls_name, bases, classdict, **kwds)
        return cls

    def __init__(cls, name, bases, classdict, **kwds: Any):
        super().__init__(name, bases, classdict, **kwds)
        _aliases = cls.__dict__["_aliases"].value
        for member_name in cls.__members__:
            if member_name.startswith("_"):
                continue
            if not member_name.isupper():
                continue
            member = cls.__dict__[member_name]
            if not isinstance(member.value, tuple):
                continue
            member.value = member.value[0]
            for unit_name in member.value:
                _aliases[unit_name] = member

    def __call__(cls, value):
        if isinstance(value, str):
            aliases = cls.__dict__["_aliases"].value
            value = value.lower()
            try:
                return aliases[value]
            except KeyError:
                raise ValueError(f"Invalid time unit: {value}")
        return super().__call__(value)


class TimeUnit(Enum, metaclass=TimeUnitMeta):
    """
    This class represents time units.
    """
    NANO_SECONDS = "ns", "nanosec", "nanosecs", "nanosecond", "nanoseconds"
    MICRO_SECONDS = "us", "microsecond", "microseconds"
    MILLI_SECONDS = "ms", "millisecond", "milliseconds"
    SECONDS = "s", "sec", "secs", "second", "seconds"
    MINUTES = "m", "min", "mins", "minute", "minutes"
    HOURS = "h", "hr", "hour", "hours"
    DAYS = "d", "day", "days"
    WEEKS = "w", "wk", "weeks"
    YEARS = "y", "yr", "year", "years"

TimeUnit("ns")


def make_time(**units):
    pass


class ITimeStamp(Interface):
    def epoch(self):
        pass


class TimeStampMeta(InterfaceMeta):
    @staticmethod
    def now():
        return time_ns()

    def __call__(self, *args: Any, **kwds: Any) -> Any:
        cur_epoch_ns = kwds.get("epoch_ns", None)
        if not args:
            cur_epoch_ns = self.now()

        inst = super().__call__(*args, **kwds)
        inst._epoch_ns = cur_epoch_ns
        return inst


class TimeStamp(ITimeStamp, metaclass=TimeStampMeta):
    """
    This class represents a time stamp.
    """
    EPOCH_MAP = {
        TimeUnit.NANO_SECONDS: 1,
        TimeUnit.MICRO_SECONDS: 1000,
        TimeUnit.MILLI_SECONDS: 1000000,
        TimeUnit.SECONDS: 1000000000,
        TimeUnit.MINUTES: 60000000000,
        TimeUnit.HOURS: 3600000000000,
        TimeUnit.DAYS: 86400000000000,
        TimeUnit.WEEKS: 604800000000000,
        TimeUnit.YEARS: 31536000000000000
    }

    def __eq__(self, other):
        """
        Compares two TimeStamp objects for equality.
        """
        return self._epoch_ns == other._epoch_ns

    def epoch(self, unit = TimeUnit.NANO_SECONDS):
        return self._epoch_ns / self.EPOCH_MAP[TimeUnit(unit)]

    def __str__(self):
        iso = pydt.fromtimestamp(self.epoch('s')).isoformat()
        iso += str(self._epoch_ns)[-3:]
        return f"[{iso}]"

    def __repr__(self):
        return f"TimeStamp({self._epoch_ns})"



ts = TimeStamp()
print(ts.epoch)
print(str(ts))