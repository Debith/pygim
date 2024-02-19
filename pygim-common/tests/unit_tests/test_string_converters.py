# -*- coding: utf-8 -*-
#type: ignore
""" Test string converter functions. """

import pytest

from pygim.utils import to_snake_case, to_human_case, to_kebab_case, to_pascal_case, to_camel_case


# create tests for to_ functions
@pytest.mark.parametrize('func,input,expected_result', [
    (to_snake_case, "HelloWorld", "hello_world"),
    (to_snake_case, "hello World", "hello_world"),
    (to_snake_case, "hello world", "hello_world"),
    (to_snake_case, "helloWorld", "hello_world"),
    (to_snake_case, "Hello World", "hello_world"),
    (to_snake_case, "hello_world", "hello_world"),
    (to_snake_case, "Hello_World", "hello_world"),
    (to_snake_case, "hello-world", "hello_world"),
    (to_snake_case, "Hello-World", "hello_world"),
    (to_snake_case, "hello-hello-great-world", "hello_hello_great_world"),
    (to_snake_case, "Hello greatNew-BeautifulWorld", "hello_great_new_beautiful_world"),
    (to_snake_case, " Hello  World  ", "hello_world"),
    (to_snake_case, "__Hello-World--", "hello_world"),

    (to_kebab_case, "HelloWorld", "hello-world"),
    (to_kebab_case, "hello World", "hello-world"),
    (to_kebab_case, "hello world", "hello-world"),
    (to_kebab_case, "helloWorld", "hello-world"),
    (to_kebab_case, "Hello World", "hello-world"),
    (to_kebab_case, "hello_world", "hello-world"),
    (to_kebab_case, "Hello_World", "hello-world"),
    (to_kebab_case, "hello-world", "hello-world"),
    (to_kebab_case, "Hello-World", "hello-world"),
    (to_kebab_case, "hello-hello-great-world", "hello-hello-great-world"),
    (to_kebab_case, "Hello greatNew-BeautifulWorld", "hello-great-new-beautiful-world"),
    (to_kebab_case, " Hello  World  ", "hello-world"),
    (to_kebab_case, "__Hello-World--", "hello-world"),

    (to_human_case, "HelloWorld", "Hello World"),
    (to_human_case, "hello World", "Hello World"),
    (to_human_case, "hello world", "Hello World"),
    (to_human_case, "helloWorld", "Hello World"),
    (to_human_case, "Hello World", "Hello World"),
    (to_human_case, "hello_world", "Hello World"),
    (to_human_case, "Hello_World", "Hello World"),
    (to_human_case, "hello-world", "Hello World"),
    (to_human_case, "Hello-World", "Hello World"),
    (to_human_case, "hello-hello-great-world", "Hello Hello Great World"),
    (to_human_case, "Hello greatNew-BeautifulWorld", "Hello Great New Beautiful World"),
    (to_human_case, " Hello  World  ", "Hello World"),
    (to_human_case, "__Hello-World--", "Hello World"),

    (to_pascal_case, "HelloWorld", "HelloWorld"),
    (to_pascal_case, "hello World", "HelloWorld"),
    (to_pascal_case, "hello world", "HelloWorld"),
    (to_pascal_case, "helloWorld", "HelloWorld"),
    (to_pascal_case, "Hello World", "HelloWorld"),
    (to_pascal_case, "hello_world", "HelloWorld"),
    (to_pascal_case, "Hello_World", "HelloWorld"),
    (to_pascal_case, "hello-world", "HelloWorld"),
    (to_pascal_case, "Hello-World", "HelloWorld"),
    (to_pascal_case, "hello-hello-great-world", "HelloHelloGreatWorld"),
    (to_pascal_case, "Hello greatNew-BeautifulWorld", "HelloGreatNewBeautifulWorld"),
    (to_pascal_case, " Hello  World  ", "HelloWorld"),
    (to_pascal_case, "__Hello-World--", "HelloWorld"),

    (to_camel_case, "HelloWorld", "helloWorld"),
    (to_camel_case, "hello World", "helloWorld"),
    (to_camel_case, "hello world", "helloWorld"),
    (to_camel_case, "helloWorld", "helloWorld"),
    (to_camel_case, "Hello World", "helloWorld"),
    (to_camel_case, "hello_world", "helloWorld"),
    (to_camel_case, "Hello_World", "helloWorld"),
    (to_camel_case, "hello-world", "helloWorld"),
    (to_camel_case, "Hello-World", "helloWorld"),
    (to_camel_case, "hello-hello-great-world", "helloHelloGreatWorld"),
    (to_camel_case, "Hello greatNew-BeautifulWorld", "helloGreatNewBeautifulWorld"),
    (to_camel_case, " Hello  World  ", "helloWorld"),
    (to_camel_case, "__Hello-World--", "helloWorld"),
])
def test_string_converters(func, input, expected_result):
    if func(input) != expected_result:
        raise AssertionError(f"For input ``{input}`` : ``{func(input)}`` != ``{expected_result}``")


if __name__ == "__main__":
    from pygim.testing import run_tests
    run_tests(__file__, to_snake_case.__module__, coverage=False)