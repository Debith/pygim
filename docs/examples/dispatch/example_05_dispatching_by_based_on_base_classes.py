# Import necessary modules
from pygim.performance.dispatch import dispatch

# In this example, dispatch decorator is used to handle different types of class instances.
# Specifically, it is demonstrated that a function registered to handle instances of a base
# class will also be invoked for instances of subclasses of that base class. The benefit of
# this approach is that you can write generic functions that work for a base class and all
# its subclasses, simplifying your code when the same logic should apply to an entire class
# hierarchy.

# Define a base class named `BaseClass`
class BaseClass:
    pass

# Define two subclasses, `SubClass1` and `SubClass2`, which inherit from `BaseClass`
class SubClass1(BaseClass):
    pass

class SubClass2(BaseClass):
    pass

# Define a dispatch function named dispatch_by_base_class, which takes an instance of a class
# This function will have multiple implementations registered, each handling a specific type of input
@dispatch
def dispatch_by_base_class(__class_instance):
    # If no registered function can handle the given arguments, raise a NotImplementedError
    raise NotImplementedError(f"Not implemented for `{__class_instance.__class__.__name__}`")

# Register a function to handle cases where the argument is an instance of `BaseClass`
# This function will also be invoked when the argument is an instance of a `BaseClass`
@dispatch_by_base_class.register(BaseClass)
def _(__subclass):
    # The function returns a string indicating it has received an instance of the class
    return f"Got `{__subclass.__class__.__name__}`"

# Test the dispatch function with instances of `BaseClass`, `SubClass1`, and `SubClass2`
assert dispatch_by_base_class(BaseClass()) == "Got `BaseClass`"
assert dispatch_by_base_class(SubClass1()) == "Got `SubClass1`"
assert dispatch_by_base_class(SubClass2()) == "Got `SubClass2`"

# Define a class named ClassDispatchExample to demonstrate dispatch within a class
class ClassDispatchExample:
    # Define a method named dispatch_by_base_class inside the class and decorate it with @dispatch
    @dispatch
    def dispatch_by_base_class(self):
        # If no registered function can handle the given arguments, raise a NotImplementedError
        raise NotImplementedError()

    # Register a method to handle cases where the argument is an instance of `BaseClass`
    # This method will also be invoked when the argument is an instance of a `BaseClass`
    @dispatch_by_base_class.register(BaseClass)
    def _(self, __subclass):
        # The method returns a string indicating it has received an instance of the class
        return f"Got `{__subclass.__class__.__name__}`"

# Create an instance of ClassDispatchExample
example = ClassDispatchExample()

# Test the dispatch method with instances of `BaseClass`, `SubClass1`, and `SubClass2`
assert example.dispatch_by_base_class(BaseClass()) == 'Got `BaseClass`'
assert example.dispatch_by_base_class(SubClass1()) == 'Got `SubClass1`'
assert example.dispatch_by_base_class(SubClass2()) == 'Got `SubClass2`'
