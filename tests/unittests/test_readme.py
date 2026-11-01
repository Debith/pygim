# -*- coding: utf-8 -*-
"""README.rst is PyPI's project page: the release refuses an sdist whose README
PyPI would not render (``twine check --strict``). Rendering it here, as
strictly, fails a pull request instead of a release.
"""

import pathlib

import pytest

README = pathlib.Path(__file__).resolve().parents[2] / "README.rst"

pytestmark = pytest.mark.skipif(not README.exists(), reason="needs the repository checkout")


def test_the_readme_renders_on_pypi():
    from docutils.core import publish_doctree
    from docutils.utils import SystemMessage

    # twine check --strict renders with readme_renderer, which halts on any
    # docutils warning; report_level 5 keeps the messages out of the output.
    settings = {"halt_level": 2, "report_level": 5, "raw_enabled": False,
                "file_insertion_enabled": False, "_disable_config": True}
    try:
        publish_doctree(README.read_text(encoding="utf-8"), settings_overrides=settings)
    except SystemMessage as e:
        pytest.fail(f"PyPI would not render README.rst: {e}")
