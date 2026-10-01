# -*- coding: utf-8 -*-
"""Fast, reproducible random numbers in the container you ask for.

``Rng(seed)`` draws from 16 interleaved xoshiro256++ streams, with AVX2 when
the CPU has it and threads for large requests. The values are a pure function
of the seed: the same on every code path, thread count and way of splitting a
request.

>>> from pygim.rng import Rng
>>> Rng(42).random(2)                   # a list by default
[0.9206594645776868, 0.8703150972100593]
>>> Rng(42).uint64(2, format="array")
array('Q', [16983169522103054190, 16054479861719614996])

``format`` picks the container: ``"list"`` (the default), ``"tuple"`` or
``"array"`` (``array.array``) need nothing beyond Python; ``"numpy"`` needs
numpy, which pygim does not depend on, and ``"polars.Series"`` or
``"polars.DataFrame"`` give one column named ``value``. ``Rng.formats()``
lists the ones this interpreter can produce. The engine only ever fills
buffers: ``rng.fill(out)`` writes straight into one you own.
"""

from array import array
from importlib import import_module
from importlib.util import find_spec
from operator import index

from pygim._rng import Rng as _Engine

__all__ = ["Rng"]


def _array(code, n):
    return array(code, [0]) * n


def _ndarray(code, n):
    return import_module("numpy").empty(n, code)


def _arrow_buffer(code, n):
    return memoryview(import_module("pyarrow").allocate_buffer(8 * n)).cast(code)


def _series(view):
    # polars takes the filled Arrow buffer (view.obj) as is, without a copy;
    # pyarrow and polars are both pygim dependencies.
    pa, pl = import_module("pyarrow"), import_module("polars")
    kind = pa.float64() if view.format == "d" else pa.uint64()
    return pl.Series("value", pa.Array.from_buffers(kind, len(view), [None, view.obj]))


# name: (library it needs, allocate(typecode, n), finish(filled buffer))
_FORMATS = {
    "list": (None, _array, array.tolist),
    "tuple": (None, _array, tuple),
    "array": (None, _array, None),
    "numpy": ("numpy", _ndarray, None),
    "polars.Series": ("polars", _arrow_buffer, _series),
    "polars.DataFrame": ("polars", _arrow_buffer, lambda view: _series(view).to_frame()),
}


class Rng(_Engine):
    """Deterministic high-throughput random generator.

    ``Rng(seed=None, *, threads=0, simd=True)``: ``seed=None`` draws fresh
    entropy; ``threads=0`` sizes large fills to the machine; ``simd=False``
    takes the scalar path, which gives the same values.

    Instances are internally locked: concurrent calls from Python threads are
    safe but serialize (and their interleaving order is not deterministic).
    For parallel workloads prefer one Rng per thread with distinct seeds; a
    single fill already parallelizes internally.
    """

    def random(self, n, *, format="list"):
        """``n`` floats uniform in [0, 1), as ``format``.

        Each is ``(x >> 11) * 2**-53`` of a raw draw ``x``, numpy's mapping.
        """
        return self._draw("d", n, format)

    def uint64(self, n, *, format="list"):
        """``n`` raw 64-bit draws, as ``format``."""
        return self._draw("Q", n, format)

    @staticmethod
    def formats():
        """The ``format`` names this interpreter can produce."""
        return tuple(name for name, (library, _, _) in _FORMATS.items() if library is None or find_spec(library))

    def _draw(self, typecode, n, format):
        try:
            _, allocate, finish = _FORMATS[format]
        except KeyError:
            raise ValueError(f"format must be one of {', '.join(map(repr, _FORMATS))}, not {format!r}") from None
        n = index(n)
        if n < 0:
            raise ValueError("n must be non-negative")
        out = allocate(typecode, n)
        self.fill(out)
        return finish(out) if finish else out
