# -*- coding: utf-8 -*-
"""pygim.core's development helpers: measure_coverage, run_tests, quick_profile and get_schema.
None had a test (found 2026-10-31); coverage is the [test] extra's, so a stand-in for it records
what the helpers ask of it."""

import ast
import inspect
import sys
import types
import typing as t

import pytest

from pygim.core import testing
from stub_parity import Stub
from pygim.core.funcs import get_schema
from pygim.core.timing_and_profiling import quick_profile


@pytest.fixture
def coverage_calls(monkeypatch):
    calls = []

    class Coverage:
        def start(self):
            calls.append("start")

        def stop(self):
            calls.append("stop")

        def save(self):
            calls.append("save")

        def report(self, include=None, show_missing=True):
            calls.append(("report", include, show_missing))

    monkeypatch.setitem(sys.modules, "coverage", types.SimpleNamespace(Coverage=Coverage))
    return calls


def test_measure_coverage_starts_then_stops_saves_and_reports(coverage_calls):
    with testing.measure_coverage(include="m.py", show_missing=False):
        coverage_calls.append("body")
    assert coverage_calls == ["start", "body", "stop", "save", ("report", "m.py", False)]


def test_measure_coverage_stops_and_reports_when_the_code_raises(coverage_calls):
    """Found 2026-10-31: no finally around the yield, so a failing body left coverage running."""
    with pytest.raises(ZeroDivisionError):
        with testing.measure_coverage(include="m.py"):
            1 / 0
    assert coverage_calls == ["start", "stop", "save", ("report", "m.py", True)]


def test_the_testing_stub_gives_each_functions_parameters():
    """Found 2026-10-31: the stub said measure_coverage returned an Iterator, which has no
    __enter__, and that run_tests' module_name had to be given."""
    stub = Stub(testing)
    assert stub.names() == set(testing.__all__)
    for name in testing.__all__:
        declared = [(n, kw, d is not Stub.NONE_GIVEN) for n, kw, d in Stub.params(stub.function(name), method=False)]
        actual = [(p.name, p.kind is p.KEYWORD_ONLY, p.default is not p.empty)
                  for p in inspect.signature(getattr(testing, name)).parameters.values()]
        assert declared == actual, name
    assert ast.unparse(stub.function("measure_coverage").returns) == "ContextManager[None]"


@pytest.fixture
def pytest_runs(monkeypatch):
    runs = []
    monkeypatch.setattr(testing.pytest, "main", lambda args: runs.append(list(args)))
    return runs


def test_run_tests_runs_the_file_with_short_tracebacks(pytest_runs, coverage_calls):
    testing.run_tests("test_x.py", coverage=False)
    assert pytest_runs == [["test_x.py", "--tb=short"]]
    assert coverage_calls == []


def test_run_tests_passes_its_own_arguments(pytest_runs):
    testing.run_tests("test_x.py", pytest_args=["-q", "test_x.py"], coverage=False)
    assert pytest_runs == [["-q", "test_x.py"]]


def test_run_tests_measures_the_module_it_names(pytest_runs, coverage_calls, tmp_path, monkeypatch):
    (tmp_path / "measured_here.py").write_text("VALUE = 1\n")
    monkeypatch.syspath_prepend(str(tmp_path))
    import measured_here

    monkeypatch.setitem(sys.modules, "measured_here", measured_here)
    testing.run_tests("test_x.py", "measured_here")
    assert pytest_runs == [["test_x.py", "--tb=short"]]
    assert coverage_calls == ["start", "stop", "save", ("report", measured_here.__file__, True)]


def test_run_tests_refuses_a_module_that_is_not_imported(pytest_runs, coverage_calls):
    with pytest.raises(ValueError, match="'never_imported_here' is not imported"):
        testing.run_tests("test_x.py", "never_imported_here")
    assert pytest_runs == []


def test_run_tests_refuses_a_module_without_a_file(pytest_runs, coverage_calls, monkeypatch):
    monkeypatch.setitem(sys.modules, "fileless_here", types.ModuleType("fileless_here"))
    with pytest.raises(ValueError, match="'fileless_here' has no file"):
        testing.run_tests("test_x.py", "fileless_here")
    assert pytest_runs == []


def test_quick_profile_prints_the_calls_it_saw(capsys):
    with quick_profile(top=3, sort="cumtime"):
        sorted(range(1000), key=lambda n: -n)
    out = capsys.readouterr().out
    assert "function calls" in out
    assert "sorted" in out


def test_quick_profile_stops_profiling_when_the_code_raises(capsys):
    with pytest.raises(ValueError):
        with quick_profile(top=1):
            raise ValueError("stop")
    assert "function calls" in capsys.readouterr().out
    assert sys.getprofile() is None


def test_get_schema_describes_each_annotated_parameter():
    def lookup(self, name: str, limit: int, ratio: float = 0.5, exact: bool = False, tags: t.List[str] = ()):
        return None

    assert get_schema(lookup) == {
        "type": "object",
        "properties": {
            "name": {"type": "string"},
            "limit": {"type": "integer"},
            "ratio": {"type": "number"},
            "exact": {"type": "boolean"},
            "tags": {"type": "array", "items": {"type": "string"}},
        },
        "required": ["name", "limit"],
    }


def test_get_schema_names_every_parameter_it_cannot_describe():
    def loose(a, *rest: int, **options: int):
        return None

    with pytest.raises(TypeError) as refused:
        get_schema(loose)
    assert str(refused.value).splitlines() == [
        "loose — schema generation failed:",
        " • parameter 'a' lacks an explicit annotation",
        " • variadic parameter 'rest' is not supported",
        " • variadic parameter 'options' is not supported",
    ]


def test_get_schema_describes_an_optional_parameter_as_it_or_null():
    def find(limit: t.Optional[int], name: str | None = None):
        return None

    assert get_schema(find)["properties"] == {
        "limit": {"anyOf": [{"type": "integer"}, {"type": "null"}]},
        "name": {"anyOf": [{"type": "string"}, {"type": "null"}]},
    }


def test_get_schema_describes_a_mapping_by_its_values():
    def tally(scores: t.Dict[str, float], names: dict[str, list[str]], anything: dict):
        return None

    assert get_schema(tally)["properties"] == {
        "scores": {"type": "object", "additionalProperties": {"type": "number"}},
        "names": {"type": "object", "additionalProperties": {"type": "array", "items": {"type": "string"}}},
        "anything": {"type": "object"},
    }


def test_get_schema_describes_any_value_as_any():
    def store(value: t.Any, values: list):
        return None

    assert get_schema(store)["properties"] == {"value": {}, "values": {"type": "array", "items": {}}}


def test_get_schema_refuses_a_type_it_has_no_form_for():
    """Found 2026-10-31: a type it did not know was described as a string — a dict, an
    Optional[int], a tuple alike (D79, Debith: refuse what it cannot describe)."""
    class Point:
        pass

    def place(pair: tuple[int, int], point: Point, keyed: dict[int, str], either: int | str):
        return None

    with pytest.raises(TypeError) as refused:
        get_schema(place)
    assert str(refused.value).splitlines() == [
        "place — schema generation failed:",
        " • parameter 'pair' has type tuple[int, int], which JSON Schema has no form for here",
        " • parameter 'point' has type Point, which JSON Schema has no form for here",
        " • parameter 'keyed' has type dict[int, str], which JSON Schema has no form for here",
        " • parameter 'either' has type int | str, which JSON Schema has no form for here",
    ]
