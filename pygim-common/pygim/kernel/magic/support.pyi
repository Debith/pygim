from typing import Any
from pygim.typing import AnyCallable

class classproperty:
    fget: AnyCallable
    def __init__(self, fget: AnyCallable) -> None: ...
    def __get__(self, __instance: Any, __class: Any) -> Any: ...
