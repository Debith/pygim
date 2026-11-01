from typing import Any, ContextManager, Iterable, Optional, Text
from _pygim.typing import PathLike

def measure_coverage(
    *, include: Optional[PathLike] = ..., show_missing: bool = ...
) -> ContextManager[None]: ...
def run_tests(
    test_file: PathLike,
    module_name: Optional[Text] = ...,
    pytest_args: Optional[Iterable[Any]] = ...,
    *,
    coverage: bool = ...,
) -> None: ...
