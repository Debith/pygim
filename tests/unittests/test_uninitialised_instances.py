"""No pygim object can be used before it is constructed.

Python can make an instance without running __init__ — `Cls.__new__(Cls)`, which
copy and pickle machinery also do — and pybind11 then hands any method called on
it uninitialised memory as the C++ object: the process dies. Every extension
refuses such use with a TypeError naming the class (utils/initialised.h).

The check runs in a child process, which names each class before trying it, so a
crash reports the class that caused it.
"""

import json
import os
import subprocess
import sys

import pytest

from pygim import pathlike

EXTENSIONS = ["pygim.pathlike", "pygim.registry", "pygim.factory", "pygim.ioc", "pygim.each", "pygim.utils",
              "pygim.datagen", "pygim._persistence", "pygim._persistence_test", "pygim._fetch_benchmark"]

CHILD = r"""
import importlib, json, sys, types
import pyarrow   # the Arrow-linked extensions need its libraries loaded first, as pygim's own wrappers do

def classes(module, seen):
    for value in list(vars(module).values()):
        if isinstance(value, types.ModuleType) and value.__name__.startswith(module.__name__) and value not in seen:
            seen.add(value)
            yield from classes(value, seen)
        elif isinstance(value, type) and type(value).__name__ == "pybind11_type" and value not in seen:
            seen.add(value)
            yield value
            for inner in vars(value).values():   # classes bound inside a class
                if isinstance(inner, type) and type(inner).__name__ == "pybind11_type" and inner not in seen:
                    seen.add(inner)
                    yield inner

def takes_only_self(method):
    head = (method.__doc__ or "").splitlines()[0] if method.__doc__ else ""
    inside = head[head.find("(") + 1:head.rfind(")")] if "(" in head else "?"
    return inside.strip().startswith("self") and "," not in inside

report = []
for name in json.load(sys.stdin):
    module = importlib.import_module(name)
    for cls in classes(module, {module}):
        if not cls.__module__.startswith(name):
            continue
        print(json.dumps(["class", f"{cls.__module__}.{cls.__qualname__}"]), flush=True)
        obj = cls.__new__(cls)
        for attr, value in vars(cls).items():
            if attr in ("__init__", "__new__", "__setstate__", "__class__", "__doc__", "__module__", "__dict__"):
                continue
            kind = type(value).__name__
            if kind not in ("property", "instancemethod") or (kind == "instancemethod" and not takes_only_self(value)):
                continue
            try:
                got = getattr(obj, attr)
                if kind == "instancemethod":
                    got()
                report.append([cls.__qualname__, attr, "answered"])
            except TypeError as e:
                report.append([cls.__qualname__, attr, "refused" if cls.__name__ in str(e) and "__init__" in str(e) else "TypeError: " + str(e)[:100]])
            except Exception as e:
                report.append([cls.__qualname__, attr, type(e).__name__ + ": " + str(e)[:100]])
print(json.dumps(["report", report]), flush=True)
"""


# Methods that never read the C++ value (they take `self` as a Python object), so they may answer.
NEEDS_NO_VALUE = {
    ("_PathSetIterator", "__iter__"): "an iterator is its own iterator",
    ("Document", "__copy__"): "immutable: a copy is the document itself",
}


def test_every_class_refuses_use_before_its_init_ran():
    proc = subprocess.run([sys.executable, "-c", CHILD], input=json.dumps(EXTENSIONS), capture_output=True,
                          text=True, timeout=300, env=os.environ.copy())
    events = [json.loads(line) for line in proc.stdout.splitlines() if line.startswith("[")]
    last = next((e[1] for e in reversed(events) if e[0] == "class"), None)
    assert proc.returncode == 0, f"the process died (rc={proc.returncode}) using an uninitialised {last}\n{proc.stderr[-1500:]}"
    report = next(e[1] for e in events if e[0] == "report")
    tried = {cls for cls, _, _ in report}
    assert len(tried) > 20, tried   # the walk found the classes
    wrong = [r for r in report if r[2] != "refused" and not (r[2] == "answered" and (r[0], r[1]) in NEEDS_NO_VALUE)]
    assert not wrong, wrong[:10]


def test_the_refusal_names_the_class_and_the_way_to_build_it():
    with pytest.raises(TypeError, match=r"path was made by __new__ without running __init__"):
        pathlike.path.__new__(pathlike.path).name
