import pygim.exceptions as ex
from .cached_type import CachedTypeMeta

class EntangledError(ex.GimError): ...
class EntangledClassError(EntangledError): ...
class EntangledMethodError(EntangledError): ...

def overrideable(func): ...
def overrides(func): ...

class _NameSpace(metaclass=CachedTypeMeta):
    def __setitem__(self, key, value) -> None: ...
    def __getitem__(self, key): ...
    def __init__(self, _name, _classes) -> None: ...

class EntangledClassMetaMeta(type):
    def __new__(mcls, name, bases, namespace): ...
    def __call__(self, _class_name, _bases, _namespace): ...

class EntangledClassMeta(type, metaclass=EntangledClassMetaMeta):
    @classmethod
    def __prepare__(cls, name, bases): ...
    def __new__(mcls, name, bases, namespace, *args): ...
    def __getitem__(self, _key): ...
    def __call__(self, *args, **kwds): ...

class EntangledClass(metaclass=EntangledClassMeta): ...
