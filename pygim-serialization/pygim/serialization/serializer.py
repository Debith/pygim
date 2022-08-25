import json
import pickle
from dataclasses import dataclass, asdict
#from attrs import define, asdict

class Callable:
    def __get__(self, __instance, __class):
        self._class = __class
        self._instance = __instance

        return self

    def __call__(self, *args, **kwargs):
        return self._instance.__dict__


class Serializer:
    serializers = dict(
        pickle=pickle.dumps,
        json=json.dumps,
        )
    serializers[None] = serializers['pickle']

    def __init__(self, *, serialize_name='dumps', deserialize_name='loads'):
        self._serialize_name = serialize_name
        self._deserialize_name = deserialize_name

    def __get__(self, __instance, __class):
        self._class = __class
        self._instance = __instance
        return self

    def __set_name__(self, __class, __name):
        assert __class is not None
        self._class = __class
        self._name = __name

        setattr(self._class, self._serialize_name, Callable())
        setattr(self._class, self._deserialize_name, Callable())

    def serialize(self, how=None):
        if not how:
            return pickle.dumps(self._instance)
        result = getattr(self._class, self._serialize_name)()
        return self.serializers[how](result)

    def deserialize(self, how):
        return getattr(self._class, self._deserialize_name)()

import inspect

def serializable(maybe_cls=None, **kwargs):
    def decorator(__class):
        __class.__serializer__ = Serializer(**kwargs)
        __class.__serializer__.__set_name__(__class, '__serializer__')
        return __class

    if inspect.isclass(maybe_cls):
        return decorator(maybe_cls)
    else:
        assert maybe_cls is None
        return decorator



def serialize(obj, *, how=None):
    try:
        return obj.__serializer__.serialize(how)
    except AttributeError:
        if isinstance(obj, list):
            return [serialize(o, how=how) for o in obj]


@serializable
@dataclass
class DeepTest:
    value: int = 0


@serializable
@dataclass
class Test:
    another: DeepTest


t = Test(DeepTest(100))
print(serialize([t], how="json"))
print(pickle.dumps([t]))
