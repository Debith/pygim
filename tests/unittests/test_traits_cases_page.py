# -*- coding: utf-8 -*-
"""The authored tables in traits_cases.py are well formed, and docs/design/traits_cases.md — rendered
from the generated cases and the authored rows — is current, so the page and the tests can never show
different cases."""

import sys

import pytest

import traits_cases
from traits_cases import TABLES, one_input_groups


@pytest.fixture(scope="module")
def naming_cases():
    return traits_cases.python_names_callspace.cases()


def test_identical_inputs_share_expected_columns(naming_cases):
    models = traits_cases.python_names_callspace.MODELS
    entries = [(models[name], naming_cases[name]) for name in ("mangled", "unmangled")]
    groups = traits_cases.model_groups(entries)
    assert len(groups) == 1
    # The source order is irrelevant: outcomes must be joined by actual arguments.
    reversed_entries = [entries[0], (entries[1][0], list(reversed(entries[1][1])))]
    rendered = "\n".join(traits_cases._model_section(reversed_entries, "test"))
    assert "| class_name | name | expected mangled | expected unmangled |" in rendered
    assert "| `'Trait'` | `'__x'` | `'_Trait__x'` | `''` |" in rendered
    assert "| `'Trait'` | **`'_Trait__x'`** | `'_Trait__x'` | `'__x'` |" in rendered
    assert "Expectation sources:" in rendered
    assert "| by |" not in rendered


def test_different_call_spaces_are_not_merged(naming_cases):
    models = traits_cases.python_names_callspace.MODELS
    entries = [(models["mangled"], naming_cases["mangled"]),
               (models["unmangled"], naming_cases["unmangled"][:-1])]
    assert len(traits_cases.model_groups(entries)) == 2


def test_shared_comparison_has_one_baseline_and_one_changed_input(naming_cases):
    model = traits_cases.python_names_callspace.MODELS["mangled"]
    baseline = traits_cases.BASELINES[model.name]
    rows = traits_cases.comparison_rows(model, naming_cases["mangled"], baseline)
    assert rows[0]["arguments"] == baseline
    arguments = [row["arguments"] for row in rows]
    keys = [traits_cases.callspace.key(value) for value in arguments]
    assert len(keys) == len(set(keys))
    for value in arguments[1:]:
        assert sum(value[key] != baseline[key] for key in baseline) == 1


@pytest.mark.parametrize("table", TABLES, ids=lambda table: table.title)
def test_every_row_has_a_cell_per_column_and_a_unique_id(table):
    assert all(len(row) == len(table.columns) + 1 for row in table.rows)
    ids = [row[-1] for row in table.rows]
    assert len(ids) == len(set(ids))


@pytest.mark.parametrize("table", TABLES, ids=lambda table: table.title)
def test_every_one_input_group_changes_its_column_only(table):
    for group in one_input_groups(table):
        fixed = dict(group["fixed"])
        for row in group["rows"]:
            for i in range(table.arguments):
                if i != group["varied"]:
                    assert repr(row[i]) == fixed[table.columns[i]]
        assert len({repr(row[group["varied"]]) for row in group["rows"]}) > 1


@pytest.mark.skipif(sys.version_info[:2] != traits_cases.RENDERED_ON,
                    reason="the expected values depend on the Python; the page is rendered on traits_cases.RENDERED_ON")
def test_the_page_is_current():
    if not traits_cases.PAGE.exists():
        pytest.skip("docs/ not present")
    assert traits_cases.PAGE.read_text(encoding="utf-8") == traits_cases.render_page(), (
        "regenerate with: python tests/unittests/traits_cases.py")
