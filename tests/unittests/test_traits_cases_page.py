# -*- coding: utf-8 -*-
"""The case tables in traits_cases.py are well formed, and docs/design/traits_cases.md — rendered
from them — is current, so the page and the tests can never show different cases."""

import pytest

import traits_cases
from traits_cases import TABLES, one_input_groups


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


def test_the_page_is_current():
    if not traits_cases.PAGE.exists():
        pytest.skip("docs/ not present")
    assert traits_cases.PAGE.read_text(encoding="utf-8") == traits_cases.render_page(), (
        "regenerate with: python tests/unittests/traits_cases.py")
