# -*- coding: utf-8 -*-
#type: ignore
""" Test security functions. """

import sys
import pytest
from importlib import reload
from unittest.mock import patch

from pygim.security import sha256sum
from pygim.iterables import flatten

IGNORE_MISSING_LIBRARY_TESTS = False
try:
    import numpy as np
    import pandas as pd
except ImportError:
    IGNORE_MISSING_LIBRARY_TESTS = True


@pytest.mark.parametrize('input,expected_result', [
    (1,                             "6b86b273ff34fce19d6b804eff5a3f5747ada4eaa22f1d49c01e52ddb7875b4b"),
    (1.0,                           "d0ff5974b6aa52cf562bea5921840c032a860a91a3512f7fe8f768f6bbe005f6"),
    (np.float16(1),                 "d0ff5974b6aa52cf562bea5921840c032a860a91a3512f7fe8f768f6bbe005f6"),
    (np.float32(1),                 "d0ff5974b6aa52cf562bea5921840c032a860a91a3512f7fe8f768f6bbe005f6"),
    (np.float64(1),                 "d0ff5974b6aa52cf562bea5921840c032a860a91a3512f7fe8f768f6bbe005f6"),

    ("test string",                 "d5579c46dfcc7f18207013e65b44e4cb4e2c2298f4ac457ba8f82743f31e930b"),
    (b"test string",                "d5579c46dfcc7f18207013e65b44e4cb4e2c2298f4ac457ba8f82743f31e930b"),
    (["test string"],               "3feb2935ec64c600411badcf604ac52d16774cbfd89867e9b85697154c1bced7"),
    (np.array(["test string"]),     "d6db0eca1e8d03b14f10852ecae00aed6a6fdc32af4795fb5497ba82a6b9c5b6"),
    (pd.Series(["test string"]),    "79edac45ae9328d902b71cb1e1062b639707a5408c1489e3f60eb9b6d9528f20"),
    (pd.Timestamp("2023-06-25"),    "1e183a182480452328a58610152e621bb1b20ae54f7d8ca1a7888884fc9cfd9d"),
    (np.array([["test string"], ["test string"]]),
                                    "71d66a82f7d433bc92138d1ae8742ba54049d1524e314e419cd733efa3791176"),
    (pd.Series([["test string"], ["test string"]]),
                                    "0c10e0269efc3166ad6c1ff3d71ead5fe6f5953da8b91d8bf25535499df59fc6"),
    (pd.DataFrame([dict(row1="test string")], [dict(row1="test string")]),
                                    "ccd33130e28272e15bc13567c7d3e03096d0528e7e8415b1532847325b147350"),
])
def test_shasum(input, expected_result):
    actual_result = sha256sum(input)

    if actual_result != expected_result:
        assert False, f"{actual_result} != {expected_result}"


def test_not_supported():
    try:
        sha256sum(lambda:None)
    except NotImplementedError:
        pass
    else:
        assert False


@patch.dict('sys.modules', {'numpy': None, 'pandas': None})
@pytest.mark.skipif(IGNORE_MISSING_LIBRARY_TESTS,
    reason="Issue installing Pandas and test is simple")
def test_missing_libraries():
    all_types = set(sha256sum.supported_types)
    new_mod = reload(sys.modules[sha256sum.__module__])
    new_types = set(new_mod.sha256sum.supported_types)

    missing_types = all_types - new_types
    missing_modules = set([c.__module__.split('.')[0] for c in flatten(missing_types)])

    if missing_modules != set(["numpy", "pandas"]):
        assert False


if __name__ == '__main__':
    from pygim.testing import run_tests
    run_tests(__file__, sha256sum.__module__, coverage=True)
