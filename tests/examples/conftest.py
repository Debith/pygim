import pytest


@pytest.fixture(autouse=True)
def run_around_tests():
    yield