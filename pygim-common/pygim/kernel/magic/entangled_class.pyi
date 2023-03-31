import pygim.exceptions as ex
from pygim.typing import AnyClass, AnyCallable
from _typeshed import Incomplete
from typing import *

class EntangledClassError(ex.GimError): ...

class EntangledClassMetaMeta(type):
    def __call__(self, class_name: Text, bases: Tuple[AnyClass], namespace: Dict[Text, Any], namespace_name: Text = ...) -> Any: ...  # type: ignore[override]

class EntangledClassMeta(type, metaclass=EntangledClassMetaMeta):
    def __new__(mcls: AnyClass, name: Text, bases: Tuple[AnyClass], namespace: Dict[Text, Any]) -> Any: ...
    def __getitem__(self, key: Hashable) -> Any: ...
    def __call__(self, *args: Any, **kwds: Any) -> Any: ...

class EntangledClass(metaclass=EntangledClassMeta):
    def namespace(cls) -> Text: ...
