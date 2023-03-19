from dataclasses import dataclass, field
from pygim import TypeDescriptor


class Name:
    field = TypeDescriptor()


@dataclass
class Person:
    name: Name = Name.field()
