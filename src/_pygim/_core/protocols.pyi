from typing import Any, Protocol, runtime_checkable

@runtime_checkable
class Service(Protocol): ...

@runtime_checkable
class Entity(Protocol):
    @property
    def id(self) -> Any: ...

@runtime_checkable
class RootEntity(Entity, Protocol):
    @property
    def id(self) -> Any: ...

@runtime_checkable
class ValueObject(Protocol):
    def __eq__(self, other: Any) -> bool: ...

@runtime_checkable
class LoadRepository(Protocol):
    def load(self, *args: Any, **kwargs: Any) -> Any: ...

@runtime_checkable
class SaveRepository(Protocol):
    def save(self, entity: Any, *args: Any, **kwargs: Any) -> Any: ...

@runtime_checkable
class Repository(LoadRepository, SaveRepository, Protocol):
    def load(self, *args: Any, **kwargs: Any) -> Any: ...
    def save(self, entity: Any, *args: Any, **kwargs: Any) -> Any: ...

@runtime_checkable
class DomainEvent(Protocol):
    @property
    def type(self) -> str: ...

@runtime_checkable
class Factory(Protocol):
    def create(self, *args: Any, **kwargs: Any) -> Any: ...

@runtime_checkable
class Builder(Protocol):
    def build(self) -> Any: ...

@runtime_checkable
class Specification(Protocol):
    def is_satisfied_by(self, candidate: Any) -> bool: ...

@runtime_checkable
class UnitOfWork(Protocol):
    def __enter__(self) -> Any: ...
    def __exit__(self, *args: Any) -> Any: ...

@runtime_checkable
class Policy(Protocol):
    def apply(self, *args: Any, **kwargs: Any) -> Any: ...

@runtime_checkable
class CommandHandler(Protocol):
    def handle(self, command: Any) -> Any: ...

@runtime_checkable
class QueryHandler(Protocol):
    def handle(self, query: Any) -> Any: ...
