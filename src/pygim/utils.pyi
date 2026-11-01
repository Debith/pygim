"""Type stubs for pygim.utils.

Time as one 64-bit count of nanoseconds (Duration, Timestamp, ManualTime), a timer of a block and
its laps (QuickTimer, Report, TimerSnapshot), a mapping that merges by strategy (gimdict), the
process's memory and processor, and small helpers.
"""

from collections.abc import Callable, ItemsView, Iterable, Iterator, KeysView, Mapping, MutableMapping, Sequence, ValuesView
from typing import ClassVar, Literal, TypedDict, overload


Strategy = Literal["replace", "extend", "union", "deep", "sum", "max", "min"] | Callable[..., object]

replace: str
extend: str
union: str
deep: str


class CpuRecord(TypedDict):
    l1d: int
    l2: int
    l3: int
    line: int
    avx2: bool


class Duration:
    """A length of time: one signed 64-bit count of nanoseconds. Duration(mins=1, secs=30).
    Units: years (365.2425 days, as C++ counts them), weeks, days, hours, mins, secs, msecs,
    usecs, nsecs. They add up; an amount may be negative or a fraction, rounded exactly to
    the nearest nanosecond (ties to even). Past 64 bits (about 292 years) is an OverflowError."""

    def __init__(self, **units: float) -> None: ...
    @property
    def nanoseconds(self) -> int:
        """The number itself."""
    @property
    def seconds(self) -> float:
        """The same in seconds, as a float: a view, not a second store."""
    def __int__(self) -> int: ...
    def __eq__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...
    def __lt__(self, other: Duration) -> bool: ...
    def __le__(self, other: Duration) -> bool: ...
    def __gt__(self, other: Duration) -> bool: ...
    def __ge__(self, other: Duration) -> bool: ...
    def __hash__(self) -> int: ...


class ManualTime:
    """Time that moves only when told: a clock for testing timing code exactly."""

    def __init__(self) -> None: ...
    def now(self) -> int:
        """Nanoseconds since this time started at 0."""
    def advance(self, by: Duration | None = None, **units: float) -> None:
        """Moves time by a Duration or by units: advance(mins=1, secs=30). Past 64 bits it raises
        OverflowError and time does not move."""


class Timestamp:
    """A moment: nanoseconds since the Unix epoch (1970-01-01T00:00:00Z)."""

    def __init__(self, nanoseconds: int) -> None:
        """The moment `nanoseconds` after the epoch: Timestamp(12333)."""
    @staticmethod
    def now() -> Timestamp:
        """The wall clock's current moment."""
    @property
    def nanoseconds(self) -> int:
        """Nanoseconds since the epoch."""
    def __int__(self) -> int: ...
    def __sub__(self, other: Timestamp) -> Duration:
        """The Duration from the other moment to this one."""
    def __eq__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...
    def __lt__(self, other: Timestamp) -> bool: ...
    def __le__(self, other: Timestamp) -> bool: ...
    def __gt__(self, other: Timestamp) -> bool: ...
    def __ge__(self, other: Timestamp) -> bool: ...
    def __hash__(self) -> int: ...


class TimerSnapshot:
    """What a QuickTimer measured: its name, total and laps."""

    @property
    def name(self) -> str:
        """The timer's name."""
    @property
    def total(self) -> Duration:
        """From the timer's start to its finish (or now)."""
    @property
    def laps(self) -> dict[str, Duration]:
        """{lap name: Duration}, in the order the laps first started."""


class TimerLap:
    """A lap of a QuickTimer as a with block: QuickTimer.lap(name)."""

    @property
    def name(self) -> str:
        """The lap's name."""
    @property
    def took(self) -> Duration:
        """All the lap's time so far: every block it ran."""
    def __enter__(self) -> TimerLap: ...
    def __exit__(self, exc_type: object, exc: BaseException | None, traceback: object) -> bool: ...


class Report:
    """How a QuickTimer tells what it measured: Report.silent(), Report.stderr(), Report.hooks(), Report.both(...)."""

    @staticmethod
    def silent() -> Report:
        """Tells nobody."""
    @staticmethod
    def stderr(*, started: bool = True, stopped: bool = True, summary: bool = True) -> Report:
        """Text on sys.stderr, as QuickTimer has always printed it; each kind of line on or off."""
    @staticmethod
    def hooks() -> Report:
        """Your callbacks: on_timer_started, on_started, on_stopped, on_finished."""
    @staticmethod
    def both(*reports: Report) -> Report:
        """Several reports at once, each hearing every moment in turn."""
    def on_timer_started(self, callback: Callable[[str], object]) -> None:
        """callback(timer_name) when the timer starts."""
    def on_started(self, callback: Callable[[str, str], object]) -> None:
        """callback(timer_name, lap) when a lap starts."""
    def on_stopped(self, callback: Callable[[str, str, Duration], object]) -> None:
        """callback(timer_name, lap, took) when a lap stops: took is a Duration, all the time that lap has run."""
    def on_finished(self, callback: Callable[[TimerSnapshot], object]) -> None:
        """callback(snapshot) when the timer finishes."""


class QuickTimer:
    """Measures a block and the laps inside it. clock: where 'now' comes from (the monotonic clock unless a
    ManualTime is given). report: how it tells what it measured (text on sys.stderr unless given).
    Use it in a with block: leaving it finishes the timer."""

    def __init__(self, name: str = "QuickTimer", *, clock: ManualTime | None = None, report: Report | None = None) -> None: ...
    @staticmethod
    def silent(name: str = "QuickTimer", *, clock: ManualTime | None = None) -> QuickTimer:
        """A timer that tells nobody: QuickTimer(name, clock=clock, report=Report.silent())."""
    def start_lap(self, name: str) -> None:
        """Starts the lap `name`, stopping the one running."""
    def stop_lap(self, name: str | None = None) -> Duration:
        """Stops the lap `name` (the running one when None) and returns all its time."""
    def finish(self) -> None:
        """Stops the running lap, freezes the total, tells the report: once."""
    def snapshot(self) -> TimerSnapshot:
        """What it measured so far, or at its finish."""
    @property
    def total(self) -> Duration:
        """From the start to now, or to the finish once finished."""
    @property
    def laps(self) -> dict[str, Duration]:
        """{lap name: Duration}, in the order the laps first started."""
    def lap(self, name: str) -> TimerLap:
        """The lap `name` as a with block: started as the block begins, stopped as it ends."""
    def __enter__(self) -> QuickTimer: ...
    def __exit__(self, exc_type: object, exc: BaseException | None, traceback: object) -> bool: ...


class gimdict(MutableMapping[object, object]):
    """A mutable mapping whose values merge by strategy: merge_in(key, value) and a | b combine an existing
    value with a new one by the key's strategy, else the new value's type's, else the default. The
    strategies: replace, extend, union, deep, sum, max, min (the module's constants, or a builtin: max, min, sum)."""

    def __init__(self, initial: Mapping[object, object] | None = None, **type_strategies: Strategy) -> None:
        """The values of mapping `initial`, if given; each keyword a type's strategy: gimdict(d, int=max, str=replace)."""
    def set(self, key: object, value: object) -> None:
        """d[key] = value, replacing what is there."""
    def get(self, key: object, default: object = None) -> object:
        """The value of `key`; `default` when it has none."""
    @overload
    def pop(self, key: object) -> object: ...
    @overload
    def pop(self, key: object, default: object) -> object:
        """Removes `key` and gives its value; when it has none, `default` if one is given, else KeyError."""
    def popitem(self) -> tuple[object, object]:
        """Removes the key set last and gives (key, value); KeyError when empty."""
    def clear(self) -> None:
        """Removes every key; the strategies stay."""
    def setdefault(self, key: object, default: object = None) -> object:
        """The value of `key`; when it has none, `default`, set as its value."""
    def update(self, other: Mapping[object, object] | Iterable[tuple[object, object]] = (), /, **values: object) -> None:
        """Sets each key of a mapping, each (key, value) pair, then each keyword, as d[key] = value does: replaced, not merged."""
    def keys(self) -> KeysView[object]:
        """The keys, a live view."""
    def values(self) -> ValuesView[object]:
        """The values, a live view."""
    def items(self) -> ItemsView[object, object]:
        """The (key, value) pairs, a live view."""
    def contains(self, key: object) -> bool:
        """Whether `key` has a value."""
    def set_strategy(self, key: object, strategy: Strategy) -> None:
        """How `key`'s values merge, before any type's."""
    def set_type_strategy(self, type_name: str, strategy: Strategy) -> None:
        """How values of the type named `type_name` ('int', 'list') merge."""
    def type_strategy(self, type_name: str) -> str:
        """The strategy values of the type named `type_name` merge by: set, or that type's default."""
    def default_strategy(self) -> str:
        """The strategy set for every type, or 'type-default' (each type's own)."""
    def set_default_strategy(self, strategy: Strategy) -> None:
        """How values merge when neither their key nor their type has a strategy."""
    def merge_in(self, key: object, value: object) -> None:
        """`value` merged into `key`'s by its strategy; set when `key` has none."""
    def merge(self, other: gimdict) -> gimdict:
        """A new gimdict: this one with every value of `other` merged in (a | b)."""
    def to_dict(self) -> dict[object, object]:
        """The values as a plain dict."""
    def __getitem__(self, key: object) -> object:
        """The value of `key`; KeyError when it has none."""
    def __setitem__(self, key: object, value: object) -> None:
        """Sets `key` to `value`, replacing what is there."""
    def __delitem__(self, key: object) -> None:
        """Removes `key`; KeyError when it has no value."""
    def __iter__(self) -> Iterator[object]:
        """The keys, in the order they were set."""
    def __reversed__(self) -> Iterator[object]:
        """The keys, the one set last first."""
    def __len__(self) -> int:
        """How many keys."""
    def __contains__(self, key: object) -> bool:
        """Whether `key` has a value."""
    def __eq__(self, other: object) -> bool:
        """Whether `other` is a mapping of the same items; the strategies are not compared."""
    __hash__: ClassVar[None]  # type: ignore[assignment]
    def __or__(self, other: gimdict) -> gimdict:
        """A new gimdict: this one with every value of `other` merged in."""


def calculate_rate(quantity: float, quantity_unit: str, duration: float, duration_unit: str, precision: int = 2) -> str:
    """Compute a throughput string from quantity+unit over duration+unit."""


def cpu() -> CpuRecord:
    """This machine's processor as pygim's kernels see it: data caches in bytes as one core sees them
    (l1d, l2, l3 the last level; a size the system does not report is a typical one, never 0), the
    cache line, and whether AVX2 kernels may run."""


def format_bytes_per_second(bytes_per_second: float, precision: int = 2) -> str:
    """Format a bytes-per-second value using human-readable units with configurable precision."""


def is_dunder(name: str) -> bool:
    """Whether name is a dunder name (__x__): four characters at least, two underscores at each end."""


def peak_rss_bytes() -> int:
    """The process's peak resident set size in bytes (its high-water mark)."""


def peak_rss_mb() -> float:
    """peak_rss_bytes() in mebibytes."""


def rss_bytes() -> int:
    """The process's resident set size in bytes (what the OS holds in physical memory now)."""


def rss_mb() -> float:
    """rss_bytes() in mebibytes."""


def to_csv(strings: Sequence[str], sorted: bool = False) -> str:
    """The strings with ', ' between each two, sorted first when sorted is true."""
