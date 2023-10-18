"""
Tests for URL class.
"""

import pytest
from _pygim.common_fast import Url


@pytest.mark.parametrize(
    "url,expected_result",
    [
        (Url("http://test"), "http://test"),
        (Url(["test"]), "http://test"),
        (Url(Url("http://test")), "http://test"),
        (Url(["http://"]), "http://"),
        (Url("http://test") / "sub", "http://test/sub"),
        (Url("http://test") / "/sub", "http://test/sub"),
        (Url("http://test") / "sub/", "http://test/sub"),
        (Url("http://test") / "/sub/", "http://test/sub"),
        (Url("http://test/") / "/sub/", "http://test/sub"),
        (Url("http://test/") / "/sub1/sub2/sub3", "http://test/sub1/sub2/sub3"),
        (Url("http://test/") / Url("http://test/sub"), "http://test/sub"),
        (Url("http://test") | dict(param="example"), "http://test"),
        (Url("http://test").with_params(param="example"), "http://test"),
    ],
)
def test_url(url, expected_result):
    actual_result = str(url)
    if actual_result != expected_result:
        assert False, f"Expected ``{expected_result}``, got ``{actual_result}``"


def test_unsupported_types():
    try:
        Url(123)
    except TypeError:
        pass
    else:
        assert False


def test_params():
    url = Url("http://test") | dict(param="example")

    assert url.params == dict(param="example")

    url.params["can not modify"] = 123

    assert url.params == dict(param="example")

from pygim.performance import quick_timer

def test_mass_creation():
    with quick_timer("mass creation"):
        urls = [Url(f"http://test/{i}") | dict(param="example") for i in range(1000000)]
    for i, url in enumerate(urls):
        if str(url) != f"http://test/{i}":
            assert False, f"Expected ``http://test/{i}``, got ``{url}``"


if __name__ == "__main__":
    pytest.main([__file__, "--capture=no"])
