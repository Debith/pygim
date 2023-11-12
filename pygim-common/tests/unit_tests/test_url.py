"""
Tests for URL class.
"""

from urllib.parse import urlparse
import cppimport.import_hook
import pytest
from _pygim.common_fast import Url


def make_url(url):
    mapping = dict(**urlparse(url)._asdict())
    mapping['host'] = mapping.pop("netloc")
    mapping.pop("params")
    return Url(**mapping)


@pytest.mark.parametrize(
    "url,expected_result",
    [
        #(Url(), Url()),
        #(Url(scheme="http"),        Url(scheme="http")),
        #(Url(host="host"),          Url(host="host")),
        #(Url(port=345),             Url(port=345)),
        #(Url(path="root"),          Url(path="root")),
        #(Url(path="root/sub"),      Url(path="root/sub")),
        #(Url(query="this=that"),    Url(query="this=that")),
        #(Url(fragment="this"),      Url(fragment="this")),
        #(Url("http://test"),        Url(make_url("http://test"))),
        #(Url("//root/sub"),         Url(make_url("//root/sub"))),

        #(Url("http://username:password@example.com:8080/path/to/resource?query=value#fragment"),
        #    Url(scheme="http", username="username", password="password", host="example.com",
        #        port=8080, path="/path/to/resource", query="query=value", fragment="fragment")),

        (str(Url("http://test")), "http://test"),
        (str(Url(["test"])), "http://test"),
        (str(Url(Url("http://test"))), "http://test"),
        (str(Url(["http://"])), "http://"),
        (str(Url("http://test") / "sub"), "http://test/sub"),
        (str(Url("http://test") / "/sub"), "http://test/sub"),
        (str(Url("http://test") / "sub/"), "http://test/sub"),
        (str(Url("http://test") / "/sub/"), "http://test/sub"),
        (str(Url("http://test/") / "/sub/"), "http://test/sub"),
        (str(Url("http://test/") / "/sub1/sub2/sub3"), "http://test/sub1/sub2/sub3"),
        (str(Url("http://test/") / Url("http://test/sub")), "http://test/sub"),
        (str(Url("http://test/") / Url("http://test/sub/")), "http://test/sub"),
        (Url("http://test/") / Url("http://test/sub/"), make_url("http://test/sub")),
        (str(Url("http://test/sub1") / Url("http://test/sub1/sub2")), "http://test/sub1/sub2"),
        (str(Url("http://test") | dict(param="example")), "http://test"),
        (str(Url("http://test").with_params(param="example")), "http://test"),
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
