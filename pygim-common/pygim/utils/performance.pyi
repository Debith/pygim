from typing import Text, Callable, Generator

def quick_timer(
    title: Text = ..., *, printer: Callable[[Text], None] = ...
) -> Generator[None, None, None]: ...
def quick_profile(top: int = ..., *, sort: Text = ...) -> Generator[None, None, None]: ...