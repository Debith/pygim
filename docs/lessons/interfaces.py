#type: ignore
"""
This example illustrates the use of interfaces in Python and the benefits they can
bring to our code.

Interfaces in Python are implemented using abstract base classes (ABCs) from the
`abc` module. An interface defines a set of methods that a class must implement in
order to be considered a "concrete" class that can be instantiated. This helps ensure
that objects of the class behave in the expected way.

This example demonstrates the benefits of using interfaces and how they can help us
write more modular, extensible, and maintainable code. By using interfaces, we can
separate the behavior of an object from its implementation details, making it easier
to change or replace implementations without affecting the rest of the code.

The use of interfaces in this example is also related to the SOLID design principles,
particularly the Interface Segregation Principle (ISP). The ISP states that "no client
should be forced to depend on methods it does not use". By defining a minimal interface
for the `Car` class, we avoid requiring clients of the `Race` class to know about the
implementation details of individual car classes. Instead, clients can simply depend
on the `Car` interface, which provides only the methods they need to know about.

The Interface Segregation Principle (ISP) is one of the SOLID design principles that
states that a class should not be forced to implement interfaces (methods) that it does
not use. By using the Car interface, we are able to separate the Race class from the
implementation details of individual car classes. The Race class only depends on the
Car interface, which provides only the methods that it needs to know about, rather than
having to know about the details of each individual car class.
"""

import abc

# In this example, we define a `Car` interface using ABCs. This interface defines a
# single method, `drive()`, which is required for any class that implements the `Car`
# interface. We then define two classes, `FastCar` and `SlowCar`, that implement the
# `Car` interface by providing their own implementations of the `drive()` method.
class Car(abc.ABC):
    @abc.abstractmethod
    def drive(self):
        """ This method exists to ensure that car can be driven! """


# We also define a `BrokenCar` class that inherits from `Car` but does not implement the
# `drive()` method. This class cannot be instantiated directly, as doing so would violate
# the `Car` interface.
class BrokenCar(Car):
    pass

try:
    BrokenCar()
except TypeError as e:
    assert "Can't instantiate abstract class BrokenCar with abstract method drive" == str(e)


# In this example, we have two different cars: fast and slow. Both implement
# the `drive()` method.
class FastCar(Car):
    def drive(self):
        print("high speed!")


class SlowCar(Car):
    def drive(self):
        print("snail speed!")


# We then define a `Race` class that allows us to race `Car` objects. The `Race` class
# takes advantage of the fact that any object that implements the `Car` interface is
# guaranteed to have a `drive()` method. We can therefore add any `Car` object to the
# race and be sure that it will be able to drive.
class Race:
    def __init__(self):
        self._cars = []

    def add_car(self, car: Car):
        # It is a good idea (although not mandatory) to ensure that the objects
        # sent to the `Race.add_car()` function are actually based on the Car interface.
        # This provides a level of safety, as we can be sure that only cars are
        # in the `Race`. This is also known as type safety.
        assert isinstance(car, Car)
        self._cars.append(car)

    def start(self):
        # The actual race becomes very simple as it only needs to call the `drive()``
        # function for cars. It doesn't need to know anything about the implementation
        # of the cars themselves.
        for car in self._cars:
            car.drive()


race = Race()
race.add_car(FastCar())
race.add_car(SlowCar())
race.start()    # high speed!
                # snail speed!

# In summary, the use of interfaces and the Car interface in particular, helps us write
# more modular and extensible code. It also demonstrates the benefits of the SOLID design
# principles, particularly the Interface Segregation Principle, which encourages us to
# separate behavior from implementation details, and only depend on the minimal set of
# methods required to accomplish our goals.
