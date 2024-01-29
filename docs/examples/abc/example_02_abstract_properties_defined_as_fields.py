#type: ignore

from dataclasses import dataclass
from pygim.gimmicks.abc import Interface


class ExampleInterface(Interface):
    @property
    def override_as_instance_field_no_default(self):
        pass
    
    @property
    def override_as_property(self):
        pass

    @property
    def override_as_instance_field(self):
        pass


@dataclass
class ExampleClass(ExampleInterface):
    override_as_instance_field_no_default: int
    override_as_instance_field: int = 24

    @property
    def override_as_property(self):
        return 42
    

example = ExampleClass(44)

assert example.override_as_property == 42
assert example.override_as_instance_field == 24

