# -*- coding: utf-8 -*-
'''

'''

from dataclasses import dataclass, field
import inspect

from .support import classproperty

@dataclass
class Relocator:
    _filters: list = field(default_factory=lambda: [])

    def __call__(self, target, namespace, names):
        if inspect.isclass(namespace):
            namespace = namespace.__dict__
        assert set(names).issubset(namespace)

        for name in names:
            if name in self._filters:
                continue
            setattr(target, name, namespace[name])


def combine(*classes, class_name=None, bases=()):
    class_name = class_name or "CombinedClass"
    base = type(class_name, bases, {})
    relocator = Relocator(["__dict__"])
    for __class in classes:
        relocator(base, __class, list(__class.__dict__))
    return base