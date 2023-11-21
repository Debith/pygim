# -*- coding: utf-8 -*-
def test_canary():
    # This is a canary test, which is used to ensure that the test suite is
    # running properly. It is the first test to run, and should always pass.
    # If this test fails, then even the library might not be installed properly.
    from pygim.ddd import IFactory

    assert IFactory
