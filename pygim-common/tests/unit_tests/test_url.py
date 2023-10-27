"""
Tests for URL class.
"""

import cppimport.import_hook
import pytest
from _pygim.common_fast import Url

@pytest.mark.parametrize(
    "url,expected_result",
    [
        (Url(), Url()),
        (Url(scheme="http"),        Url(scheme="http")),
        (Url(host="host"),          Url(host="host")),
        (Url(port=345),             Url(port=345)),
        (Url(path="root"),          Url(path="root")),
        (Url(path="root/sub"),      Url(path="root/sub")),
        (Url(query="this=that"),    Url(query="this=that")),
        (Url(fragment="this"),      Url(fragment="this")),
        (Url("http://test"),        Url(scheme="http", host="test")),

        #(Url("http://test"), "http://test"),
        #(Url(["test"]), "http://test"),
        #(Url(Url("http://test")), "http://test"),
        #(Url(["http://"]), "http://"),
        #(Url("http://test") / "sub", "http://test/sub"),
        #(Url("http://test") / "/sub", "http://test/sub"),
        #(Url("http://test") / "sub/", "http://test/sub"),
        #(Url("http://test") / "/sub/", "http://test/sub"),
        #(Url("http://test/") / "/sub/", "http://test/sub"),
        #(Url("http://test/") / "/sub1/sub2/sub3", "http://test/sub1/sub2/sub3"),
        #(Url("http://test/") / Url("http://test/sub"), "http://test/sub"),
        #(Url("http://test") | dict(param="example"), "http://test"),
        #(Url("http://test").with_params(param="example"), "http://test"),
    ],
)
def test_url(url, expected_result):
    actual_result = url
    if actual_result != expected_result:
        assert False, f"Expected ``{expected_result}``, got ``{actual_result}``"
"""


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


def test_mass_creation():
    urls = [Url(f"http://test/{i}") | dict(param="example") for i in range(100000)]
    for i, url in enumerate(urls):
        if str(url) != f"http://test/{i}":
            assert False, f"Expected ``http://test/{i}``, got ``{url}``"

"""

if __name__ == "__main__":
    pytest.main([__file__, "--capture=no"])
