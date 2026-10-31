# -*- coding: utf-8 -*-
"""pygim.core.timing_and_profiling.quick_timer: what it prints, which its rebuild on
pygim.utils.QuickTimer must keep."""

import re

from pygim.core.timing_and_profiling import quick_timer


def test_it_announces_the_block_then_its_time():
    said = []
    with quick_timer(printer=said.append):
        pass
    assert said[0] == "Code block started..." and re.fullmatch(r"Code block executed in \d+\.\d\d seconds!", said[1])


def test_its_title_names_both_lines():
    said = []
    with quick_timer("Slow code", printer=said.append):
        pass
    assert said[0] == "Slow code started..." and said[1].startswith("Slow code executed in ")


def test_any_title_is_said_as_its_text():
    said = []
    with quick_timer(42, printer=said.append):
        pass
    assert said[0] == "42 started..."


def test_without_the_start_line_it_says_only_the_time():
    said = []
    with quick_timer(printer=said.append, print_on_start=False):
        pass
    assert len(said) == 1 and said[0].startswith("Code block executed in ")


def test_it_reports_the_time_even_when_the_block_raises():
    said = []
    try:
        with quick_timer(printer=said.append):
            raise KeyError("x")
    except KeyError:
        pass
    assert said[-1].startswith("Code block executed in ")
