# type: ignore
""" This module tests Gimmick object. """
import pytest
import abc

from pygim.kernel.primitives.gimmick import Gim
from pygim.exceptions import GimError

class MyAbstract(Gim, abc=True):
    pass


class MyInterface(Gim, interface=True):
    pass


def test_abstract():
    assert issubclass(MyAbstract, abc.ABC)


def test_interface():
    assert issubclass(MyInterface, abc.ABC)

########################################################
######## INTERFACE #####################################
########################################################

@pytest.mark.parametrize("decorator", [
    lambda func: func,
    classmethod,
    staticmethod,
    property,
])
def test_interface_empty_functions(decorator):
    """ All methods and properties that are exposed will be made abstract and must be empty! """

    try:
        class _(Gim, interface=True):
            @decorator
            def func(self):
                """"""

    except Exception:
        assert False, "should not throw exception!"


@pytest.mark.parametrize("decorator", [
    lambda func: func,
    classmethod,
    staticmethod,
    property,
])
def test_interface_creation(decorator):
    """ Class behaves as an normal abstract class! """
    class Example(Gim, interface=True):
        @decorator
        def func(self):
            """"""

    try:
        Example()
    except GimError as e:
        assert False, f"Wrong kind of exception! {e}"
    except Exception as e:
        assert "func" in str(e)
    else:
        assert False, "Should throw exception!"


@pytest.mark.parametrize("decorator", [
    lambda func: func,
    classmethod,
    staticmethod,
    property,
])
def test_interface_non_empty_function(decorator):
    """ All methods and properties that are exposed will be made abstract and must be empty! """

    try:
        class Example(Gim, interface=True):
            @decorator
            def _non_empty_func(self):
                this = 1
                return True

    except GimError:
        pass
    except Exception as e:
        assert False, f"wrong kind of exception! {e}"
    else:
        assert False, "should throw exception!"


########################################################
######## ABC ###########################################
########################################################


@pytest.mark.parametrize("decorator", [
    lambda func: func,
    classmethod,
    staticmethod,
    property,
])
def test_abc_empty_functions(decorator):
    """ All methods and properties that are exposed will be made abstract and must be empty! """

    try:
        class _(Gim, abc=True):
            @decorator
            def func(self):
                """"""

    except Exception:
        assert False, "should not throw exception!"


@pytest.mark.parametrize("decorator", [
    abc.abstractmethod,
    abc.abstractclassmethod,
    abc.abstractstaticmethod,
    abc.abstractproperty,
])
def test_abc_creation(decorator):
    """ Class behaves as an normal abstract class! """
    class Example(Gim, abc=True):
        @decorator
        def func(self):
            """"""

    try:
        Example()
    except GimError as e:
        assert False, f"Wrong kind of exception! {e}"
    except Exception as e:
        assert "func" in str(e)
    else:
        assert False, "Should throw exception!"


@pytest.mark.parametrize("decorator", [
    abc.abstractmethod,
    abc.abstractclassmethod,
    abc.abstractstaticmethod,
    abc.abstractproperty,
])
def test_abc_non_empty_function(decorator):
    """ All methods and properties that are exposed will be made abstract and must be empty! """

    try:
        class Example(Gim, abc=True):
            @decorator
            def _non_empty_func(self):
                this = 1
                return True

    except Exception as e:
        assert False, f"wrong kind of exception! {e}"
    else:
        pass


def test_abc_included_to_locals():
    try:
        class Example(Gim, abc=True):
            TEST = 1
            @abstractmethod
            def _non_empty_func(self):
                this = 1
                return True

    except NameError as e:
        assert False, f"Should not throw an exception"


if __name__ == "__main__":
    from pygim.utils.testing import run_tests
    run_tests(__file__, Gim.__module__, coverage=False)