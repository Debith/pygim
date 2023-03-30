from dataclasses import dataclass, field
from pygim import TypeDescriptor


@dataclass
class Name:
    _name: str


@dataclass
class Person:
    name: Name = field(default_factory=Name)


person = Person("example")
assert isinstance(person.name, str)


@dataclass
class Name:
    _name: str
    field = TypeDescriptor()


@dataclass
class Person:
    name: Name = Name.field()


person = Person("new example")
assert isinstance(person.name, Name)