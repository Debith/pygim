from typing import Any, Callable, Protocol, runtime_checkable

__all__ = [
    "Closable",
    "InboundEventBus",
    "OutboundEventBus",
]

@runtime_checkable
class Closable(Protocol):
    """Structural protocol for closeable runtime collaborators."""

    def close(self) -> None: ...

@runtime_checkable
class InboundEventBus(Closable, Protocol):
    """Thin Python wrapper for the adapter-owned inbound bus control surface."""

    def start(self, submit_envelope: Callable[[Any], Any]) -> None: ...

@runtime_checkable
class OutboundEventBus(Closable, Protocol):
    """Thin Python wrapper for the adapter-owned outbound bus control surface."""

    def try_submit(self, envelope: Any) -> Any: ...