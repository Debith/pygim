# type: ignore
"""Errors: ``pygim.errors``.

Everything pygim raises is a ``GimError``, and also the Python exception its
meaning needs. One ``except GimError`` catches every failure that comes out of
pygim, while code written against Python's own contracts (``except
ValueError``, ``except KeyError``) keeps working as it is. The classes are made
once, in C++, and every pygim module raises these same class objects.

This example demonstrates:
- The classes: each a GimError and the Python exception its meaning needs
- A missing key says the keys it was not among
- Catching everything pygim raises with one ``except GimError``
- Catching the same error by its Python contract, as code written before it does
- Reading what went wrong: a GimOptionError's ``value`` and ``options``
- An error that crosses a process boundary arrives as itself
"""

import pickle

from pygim.errors import GimError, GimOptionError, MissingKeyError
from pygim.utils import gimdict

STRATEGIES = ("sum", "max", "min", "replace")  # the merge strategies gimdict knows by name

# ----------------------------------------------------------------------------
# 1. The classes: pygim's, and Python's too
# ----------------------------------------------------------------------------
assert issubclass(MissingKeyError, GimError)       # a lookup that found nothing...
assert issubclass(MissingKeyError, KeyError)       # ...is a KeyError, as a mapping's miss is
assert issubclass(GimOptionError, GimError)        # a value that is none of its options...
assert issubclass(GimOptionError, ValueError)      # ...is a ValueError, as Python's bad value is

settings = gimdict({"host": "db", "port": 5432})
try:
    settings["hots"]                               # a key it does not have
except KeyError as error:                          # a mapping's miss, caught as Python catches one
    missed = error
assert type(missed) is MissingKeyError
assert missed.args == ("hots",)                    # the key, where a KeyError keeps it
assert missed.within is settings                   # and where it was looked for
assert str(missed) == "Key ``hots`` not among: host, port"

# ----------------------------------------------------------------------------
# 2. One except clause for everything pygim raises
# ----------------------------------------------------------------------------
merged = gimdict()
#                           ┌─ strategy: the name of a merge strategy, one of STRATEGIES
#                           ▼
merged.set_default_strategy("max")                 # a name it knows: nothing raised

try:
    merged.set_default_strategy("AVG")             # a name it does not know
except GimError as error:                          # whatever pygim raised, from any module
    caught = error
assert type(caught) is GimOptionError

# ----------------------------------------------------------------------------
# 3. The same error, caught by Python's contract
# ----------------------------------------------------------------------------
try:
    merged.set_default_strategy("AVG")
except ValueError as error:                        # code that knows nothing of pygim's classes
    caught = error
assert isinstance(caught, GimOptionError)          # it caught pygim's class all the same

# ----------------------------------------------------------------------------
# 4. What went wrong: the value given and the options it was not among
# ----------------------------------------------------------------------------
assert caught.value == "AVG"
assert caught.options == STRATEGIES
assert str(caught) == "Given input ``AVG`` not among: sum, max, min, replace"

# The options are data, not only text: a caller can act on them. Here, a name
# given in the wrong case is matched against the names the error lists.
try:
    merged.set_default_strategy("MIN")
except GimOptionError as error:
    chosen = next(option for option in error.options if option == error.value.lower())
    merged.set_default_strategy(chosen)
assert chosen == "min"

# ----------------------------------------------------------------------------
# 5. An error that crosses a process boundary arrives as itself
# ----------------------------------------------------------------------------
# A worker process hands its exception to the parent pickled, as
# concurrent.futures and multiprocessing do: it comes back as the same class,
# its value and options kept.
again = pickle.loads(pickle.dumps(caught))
assert type(again) is GimOptionError
assert again.value == "AVG"
assert again.options == STRATEGIES

print("errors example OK: one except GimError, Python's contracts kept, the error says which")
