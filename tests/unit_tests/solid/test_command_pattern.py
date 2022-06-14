# -*- coding: utf-8 -*-

import pytest

from pygim.patterns import Command


def test_command_captures_callable_elegantly():
    def example():
        return "hello"

    cmd = Command(example)
    assert cmd() == "hello"


def test_command_can_override_its_call_method():
    class MyCommand(Command):
        def __call__(self):
            return "overridden hello!"

    cmd = MyCommand()
    assert cmd() == "overridden hello!"


def test_creating_chained_commands():
    cmd = Command(lambda: "hello", lambda: "chained!")
    assert cmd() == ["hello", "chained"]


if __name__ == "__main__":
    pytest.main([__file__])