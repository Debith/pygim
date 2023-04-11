class NormalClass:
    def __new__(cls, value):
        instance = super().__new__(cls)
        return instance

    def __init__(self, value):
        self._value = value

    def show(self):
        return self._value


example = NormalClass(42)
print(example.show())


class CarMeta(type):
    def __call__(self, value):
        if value > 100:
            car = Ferrari.__new__(Ferrari)
        else:
            car = Lada.__new__(Lada)

        car._value = value

        return car


class Car:
    def show(self):
        pass


class Ferrari(Car):
    def show(self):
        return self._value * 100


class Lada(Car):
    def show(self):
        return self._value / 10


example1 = Car(120)
example2 = Car(10)
print(example1.show())
print(example2.show())
print(type(example1))
print(type(example2))