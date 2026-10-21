"""pygim.traits.CodeBuilder: the cost of rebuilding a method's code for another class, against
Python's own floor — the same code objects remade by code.replace() with their own values.

Sections:

1. **build** — CodeBuilder(code).reown(Trait, Target).build() on a method naming 40 attributes
   (2 of them private) and on one whose names sit in a nested lambda, vs code.replace() walking the
   same code objects in Python and changing nothing: what Python itself pays to make them.

Run:  python benchmarks/traits_code_builder.py [--no-save]
Each run appends its raw measurements + environment metadata to
``results/traits_code_builder.jsonl`` (see ``_results.py``).
"""

import types

from tabulate import tabulate

from pygim.traits import CodeBuilder
from _results import save, wants_save

from _bench import REPS, best

LOOPS = 20_000


class Trait:
    def many(self):
        return (self.a0, self.a1, self.a2, self.a3, self.a4, self.a5, self.a6, self.a7, self.a8, self.a9,
                self.b0, self.b1, self.b2, self.b3, self.b4, self.b5, self.b6, self.b7, self.b8, self.b9,
                self.c0, self.c1, self.c2, self.c3, self.c4, self.c5, self.c6, self.c7, self.c8, self.c9,
                self.d0, self.d1, self.d2, self.d3, self.d4, self.d5, self.d6, self.d7, self.__x, self.__y)

    def nested(self):
        return (lambda: (self.a0, self.a1, self.a2, self.a3, self.a4, self.a5, self.a6, self.a7,
                         self.a8, self.a9, self.__x))()


class Target:
    pass


def python_floor(code):
    """The code object remade by code.replace() with its own values, nested code first."""
    consts = tuple(python_floor(c) if isinstance(c, types.CodeType) else c for c in code.co_consts)
    changes = {"co_names": code.co_names, "co_consts": consts}
    if hasattr(code, "co_qualname"):  # Python 3.11 and later
        changes["co_qualname"] = code.co_qualname
    return code.replace(**changes)


def names_in(code):
    return len(code.co_names) + sum(names_in(c) for c in code.co_consts if isinstance(c, types.CodeType))


def bench_build():
    rows, raw = [], {}
    for case in ("many", "nested"):
        code = getattr(Trait, case).__code__
        builder = CodeBuilder(code).reown(Trait, Target)
        ours, _ = best(lambda: [builder.build() for _ in range(LOOPS)], reps=REPS)
        floor, _ = best(lambda: [python_floor(code) for _ in range(LOOPS)], reps=REPS)
        names = names_in(code)
        raw[case] = {"names": names, "loops": LOOPS, "code_builder_seconds": ours, "python_floor_seconds": floor}
        above = (ours - floor) / LOOPS / names * 1e9
        rows.append([case, names, f"{ours / LOOPS * 1e6:.2f}", f"{floor / LOOPS * 1e6:.2f}", f"{above:,.0f}"])
    print(tabulate(rows, headers=["method", "names", "CodeBuilder µs", "Python floor µs", "above the floor, ns/name"]))
    return raw


def main():
    sections = {"build": bench_build()}
    if wants_save():
        print(f"\nRun recorded -> {save('traits_code_builder', sections, reps=REPS)}")


if __name__ == "__main__":
    main()
