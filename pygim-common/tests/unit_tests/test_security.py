# -*- coding: utf-8 -*-
#type: ignore
""" Test security functions. """
import pytest
from pygim.security import sha256sum
import numpy as np
import pandas as pd

@pytest.mark.parametrize('input,expected_result', [
    ("test string",                 "d5579c46dfcc7f18207013e65b44e4cb4e2c2298f4ac457ba8f82743f31e930b"),
    (b"test string",                "d5579c46dfcc7f18207013e65b44e4cb4e2c2298f4ac457ba8f82743f31e930b"),
    (["test string"],               "3feb2935ec64c600411badcf604ac52d16774cbfd89867e9b85697154c1bced7"),
    (np.array(["test string"]),     "d6db0eca1e8d03b14f10852ecae00aed6a6fdc32af4795fb5497ba82a6b9c5b6"),
    (np.array([["test string"], ["test string"]]), "71d66a82f7d433bc92138d1ae8742ba54049d1524e314e419cd733efa3791176"),
    (pd.Series(["test string"]),    "79edac45ae9328d902b71cb1e1062b639707a5408c1489e3f60eb9b6d9528f20"),
    (pd.Series([["test string"], ["test string"]]), "0c10e0269efc3166ad6c1ff3d71ead5fe6f5953da8b91d8bf25535499df59fc6"),
])
def test_shasum(input, expected_result):
    actual_result = sha256sum(input)

    if actual_result != expected_result:
        assert False, f"{actual_result} != {expected_result}"


if __name__ == '__main__':
    from pygim.testing import run_tests
    run_tests(__file__, sha256sum.__module__, coverage=False)
