# -*- coding: utf-8 -*-
"""
Runtime-checkable protocols for Domain-Driven Design (DDD) tactical patterns.

These are structural (duck-typed) contracts — any class that implements the
required methods/properties satisfies the protocol without explicit inheritance.
Use ``isinstance(obj, SomeProtocol)`` at runtime to verify structural conformance.

Protocols
---------
Service, Entity, RootEntity, ValueObject, LoadRepository, SaveRepository,
Repository, DomainEvent, Factory, Builder, Specification, UnitOfWork, Policy,
CommandHandler, QueryHandler.

Examples
--------
>>> class User:
...     def __init__(self, id, name):
...         self._id = id
...         self.name = name
...     @property
...     def id(self):
...         return self._id
>>> isinstance(User(1, "Alice"), Entity)
True

>>> class UserRepo:
...     def load(self, *args, **kwargs): ...
...     def save(self, entity): ...
>>> isinstance(UserRepo(), Repository)
True
"""

from __future__ import annotations

from typing import Any, runtime_checkable, Protocol

__all__ = [
    "Service",
    "Entity",
    "RootEntity",
    "ValueObject",
    "LoadRepository",
    "SaveRepository",
    "Repository",
    "DomainEvent",
    "Factory",
    "Builder",
    "Specification",
    "UnitOfWork",
    "Policy",
    "CommandHandler",
    "QueryHandler",
]


# ---------------------------------------------------------------------------
#  Core DDD Protocols
# ---------------------------------------------------------------------------

@runtime_checkable
class Service(Protocol):
    """Marker protocol for services (domain, application, infrastructure)."""
    ...


@runtime_checkable
class Entity(Protocol):
    """An object identified by a unique ``id`` property.

    Example
    -------
    >>> class User:
    ...     def __init__(self, id, name):
    ...         self._id = id
    ...         self.name = name
    ...     @property
    ...     def id(self):
    ...         return self._id
    >>> isinstance(User(1, "Alice"), Entity)
    True
    """

    @property
    def id(self) -> Any: ...


@runtime_checkable
class RootEntity(Entity, Protocol):
    """An aggregate root — an ``Entity`` that serves as the entry point to its aggregate."""

    @property
    def id(self) -> Any: ...


@runtime_checkable
class ValueObject(Protocol):
    """An immutable object defined by its attributes, not identity.

    Example
    -------
    >>> class Color:
    ...     __slots__ = ("red", "green", "blue")
    ...     def __init__(self, r, g, b):
    ...         object.__setattr__(self, "red", r)
    ...         object.__setattr__(self, "green", g)
    ...         object.__setattr__(self, "blue", b)
    ...     def __eq__(self, other):
    ...         return isinstance(other, Color) and (self.red, self.green, self.blue) == (other.red, other.green, other.blue)
    >>> isinstance(Color(1, 2, 3), ValueObject)
    True
    """

    def __eq__(self, other: Any) -> bool: ...


# ---------------------------------------------------------------------------
#  Repository Protocols
# ---------------------------------------------------------------------------

@runtime_checkable
class LoadRepository(Protocol):
    """A repository that can load/retrieve data.

    Example
    -------
    >>> class UserRepo:
    ...     def load(self, user_id):
    ...         return {"id": user_id}
    >>> isinstance(UserRepo(), LoadRepository)
    True
    """

    def load(self, *args: Any, **kwargs: Any) -> Any: ...


@runtime_checkable
class SaveRepository(Protocol):
    """A repository that can save/persist data.

    Example
    -------
    >>> class UserRepo:
    ...     def save(self, entity):
    ...         pass
    >>> isinstance(UserRepo(), SaveRepository)
    True
    """

    def save(self, entity: Any, *args: Any, **kwargs: Any) -> Any: ...


@runtime_checkable
class Repository(LoadRepository, SaveRepository, Protocol):
    """A repository that can both load and save.

    Example
    -------
    >>> class UserRepo:
    ...     def load(self, user_id): return {"id": user_id}
    ...     def save(self, entity): pass
    >>> isinstance(UserRepo(), Repository)
    True
    """

    def load(self, *args: Any, **kwargs: Any) -> Any: ...
    def save(self, entity: Any, *args: Any, **kwargs: Any) -> Any: ...


# ---------------------------------------------------------------------------
#  Domain Events
# ---------------------------------------------------------------------------

@runtime_checkable
class DomainEvent(Protocol):
    """A discrete event that domain experts care about.

    Example
    -------
    >>> class UserRegistered:
    ...     def __init__(self, user_id):
    ...         self.user_id = user_id
    ...     @property
    ...     def type(self):
    ...         return "UserRegistered"
    >>> isinstance(UserRegistered(1), DomainEvent)
    True
    """

    @property
    def type(self) -> str: ...


# ---------------------------------------------------------------------------
#  Factories & Builders
# ---------------------------------------------------------------------------

@runtime_checkable
class Factory(Protocol):
    """A factory that creates domain objects.

    Example
    -------
    >>> class UserFactory:
    ...     def create(self, id, name):
    ...         return {"id": id, "name": name}
    >>> isinstance(UserFactory(), Factory)
    True
    """

    def create(self, *args: Any, **kwargs: Any) -> Any: ...


@runtime_checkable
class Builder(Protocol):
    """A builder that constructs complex objects step by step.

    Example
    -------
    >>> class QueryBuilder:
    ...     def build(self):
    ...         return "SELECT 1"
    >>> isinstance(QueryBuilder(), Builder)
    True
    """

    def build(self) -> Any: ...


# ---------------------------------------------------------------------------
#  Tactical Patterns
# ---------------------------------------------------------------------------

@runtime_checkable
class Specification(Protocol):
    """Encapsulates a business rule that can be evaluated against a candidate.

    Example
    -------
    >>> class IsActive:
    ...     def is_satisfied_by(self, candidate):
    ...         return getattr(candidate, "active", False)
    >>> isinstance(IsActive(), Specification)
    True
    """

    def is_satisfied_by(self, candidate: Any) -> bool: ...


@runtime_checkable
class UnitOfWork(Protocol):
    """Manages a transactional boundary (context-manager protocol).

    Example
    -------
    >>> class SimpleUoW:
    ...     def __enter__(self): return self
    ...     def __exit__(self, *args): pass
    >>> isinstance(SimpleUoW(), UnitOfWork)
    True
    """

    def __enter__(self) -> Any: ...
    def __exit__(self, *args: Any) -> Any: ...


@runtime_checkable
class Policy(Protocol):
    """A policy that encapsulates a domain rule.

    Example
    -------
    >>> class DiscountPolicy:
    ...     def apply(self, order):
    ...         return order
    >>> isinstance(DiscountPolicy(), Policy)
    True
    """

    def apply(self, *args: Any, **kwargs: Any) -> Any: ...


@runtime_checkable
class CommandHandler(Protocol):
    """Handles a command (write-side in CQRS).

    Example
    -------
    >>> class RegisterUser:
    ...     def handle(self, command):
    ...         pass
    >>> isinstance(RegisterUser(), CommandHandler)
    True
    """

    def handle(self, command: Any) -> Any: ...


@runtime_checkable
class QueryHandler(Protocol):
    """Handles a query (read-side in CQRS).

    Example
    -------
    >>> class GetUser:
    ...     def handle(self, query):
    ...         return {"id": 1}
    >>> isinstance(GetUser(), QueryHandler)
    True
    """

    def handle(self, query: Any) -> Any: ...
