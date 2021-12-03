from pygim.system.utils.performance import quick_timer
import pandas as pd



class Day:
    def __init__(self, ts, index):
        self._index = index
        self._nano = ts.value
        self._datetime = ts.to_pydatetime()
        self._date = ts.date()
        self._date_str = str(self._date)
        self._timestamp = ts

    def __hash__(self):
        return self._index

    def __repr__(self):
        return self._date_str

    def __index__(self):
        return self._index

    @property
    def day(self):
        return self._date.day

    @property
    def month(self):
        return self._date.month


class Days:
    def __init__(self, days):
        self._days = days

    def __getitem__(self, key):
        return self._days[key]

    def __repr__(self):
        return f"<{self._days[0]} - {self._days[-1]}>"


GEN = ((i, ts) for i, ts in enumerate(pd.date_range("1970-01-01", "2050-12-31"))


print(len(DATES))