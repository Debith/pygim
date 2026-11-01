# -*- coding: utf-8 -*-
"""Deterministic structural similarity of functions and classes.

An ``Index`` holds the units (functions, methods, classes) of ONE language and
compares every one with every other of its kind: a Dice score
over normalised syntax subtrees, 1.0 meaning identical up to local names.
Languages never meet: a file of another language is refused, and there is no
operation between two indexes. Results are pyarrow Tables, or for
``stream_pairs()`` RecordBatchReaders, built in C++.

>>> from pygim import similarity
>>> ix = similarity.Index("python")
>>> ix.add("def f(a):\\n    return a + 1\\n\\ndef g(b):\\n    return b + 1\\n", origin="m.py")
>>> ix.pairs("function").select(["a_qualname", "b_qualname", "exact"]).to_pylist()
[{'a_qualname': 'f', 'b_qualname': 'g', 'exact': True}]
"""

# Arrow's shared libraries load before the extension that links them, so the
# loader resolves libarrow from the pyarrow package (see pygim/persistence.py).
import pyarrow  # noqa: F401

from pygim._similarity import LANGUAGES, Index, language_of

__all__ = ["LANGUAGES", "Index", "language_of"]
