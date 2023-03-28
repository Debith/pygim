from typing import Iterable, Any, Callable, Tuple, Generator

def split(iterable: Iterable[Any], condition: Callable[[Any], bool]) -> Tuple[Iterable[Any], Iterable[Any]]: ...
def is_container(obj: Any) -> bool: ...
def flatten(items: Iterable[Any]) -> Generator[Any, None, None]: ...
