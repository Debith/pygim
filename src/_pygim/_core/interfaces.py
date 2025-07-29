# -*- coding: utf-8 -*-
"""
This module provides a set of abstract interfaces aimed at simplifying the development of
applications following Domain-Driven Design (DDD) principles. These interfaces form the
foundation for modeling complex business domains.

Classes
-------
IEntity:
    An interface representing entities, which are objects with a distinct identity that
    persists over time.

IRootEntity:
    An interface representing the root of an aggregate, a cluster of associated objects
    that are treated as a unit.

IValueObject:
    An interface for value objects, which are objects that contain attributes but do not
    have a conceptual identity.

IDomainEvent:
    An interface for domain events, which are discrete events that domain experts care about.

IRepository:
    An interface for repositories, which are mechanisms for retrieving entities and
    persisting them back to the database.

IFactory:
    An interface for factories, which are used to create domain objects.

IBuilder:
    An interface for builders, a specific kind of factory that is used to build complex
    objects step by step.

IDomainService:
    An interface for domain services, which are used when a significant process or
    transformation in the domain isn't a natural responsibility of an entity or value
    object.

IService:
    A generic service interface from which the more specific domain, application and
    infrastructure service interfaces derive.

IApplicationService:
    An interface for application services, which are used to drive the interactions between
    domain layer and user interface or external systems.

IInfrastructureService:
    An interface for infrastructure services, which provide technical capabilities that
    support the layers of the application.

"""


import abc
from enum import Enum, unique
from pygim.core.explib import type_error_msg


class IService(abc.ABC):
    """
    An abstract base class that represents a Service in a software architecture.

    A Service is a part of the system's functionality that provides certain capabilities.
    These capabilities often involve interactions between different parts of the system,
    such as operations that involve multiple domain objects or that span multiple layers
    or subsystems of the architecture.

    In the context of Domain-Driven Design (DDD), a Service might be a Domain Service,
    an Application Service, or an Infrastructure Service, each having different
    responsibilities and roles within the system.

    The specific methods and properties of a Service would depend on what capabilities
    the Service provides.

    Example
    -------
    >>> class UserService(IService):
    ...     def create_user(self, username, password):
    ...         # Create a new user
    ...         pass
    """


class IEntity(abc.ABC):
    """
    An abstract base class that adheres to Domain-Driven Design (DDD) principles.

    Entity

    In Domain-Driven Design (DDD), an Entity is a core concept that is not defined
    by its attributes, but rather by a thread of continuity and its identity.
    Entities are characterized by their identity and mutable state. They have
    the same ID throughout their lifecycle, even though their state may change.

    For example, consider a `User` entity in a system. Each user would have a
    unique ID to distinguish them, even if details such as their username or
    email change.

    In Python and DDD, Entities are often represented by classes, with each
    instance representing a unique entity. These classes would have methods
    defining behavior related to the entity, and their instances would carry
    an ID to ensure identity continuity.

    This IEntity class represents such an entity, with an abstract `id` property
    and equality determined by this `id`.

    Attributes
    ----------
    id : Any, abstract
        Unique identity attribute of the entity.

    Methods
    -------
    __eq__(entity) -> bool:
        Determines equality based on id.

    Example
    -------
    >>> class User(IEntity):
    ...     def __init__(self, id, name):
    ...         self._id = id
    ...         self.name = name
    ...     @property
    ...     def id(self):
    ...         return self._id
    >>> user1 = User(1, 'Alice')
    >>> user2 = User(2, 'Bob')
    >>> user1 == user2
    False
    """

    @abc.abstractproperty
    def id(self):
        """
        Abstract property representing the unique identity attribute of the entity.

        Raises
        ------
        NotImplementedError
            If this method is not overridden in a concrete class derived from IEntity.
        """
        raise NotImplementedError()

    def __eq__(self, entity):
        """
        Defines entity equality based on their unique identifiers.

        Parameters
        ----------
        entity : IEntity
            The entity to compare with.

        Returns
        -------
        bool
            True if the entities' ids are equal, False otherwise.
        """
        assert isinstance(entity, IEntity), type_error_msg(entity, IEntity)
        return self.id == entity.id


class IRootEntity(IEntity):
    """
    An abstract base class that represents an Aggregate Root in Domain-Driven Design.

    In Domain-Driven Design (DDD), an Aggregate is a cluster of domain objects that
    can be treated as a single unit. An Aggregate Root is an entity inside the
    Aggregate that other objects are accessed through, ensuring the rules of the
    Aggregate are not violated.

    This class extends IEntity, meaning it has a unique ID and equality is based
    on this ID. As an Aggregate Root, it is expected to control and coordinate
    activities within its Aggregate.

    Attributes
    ----------
    Inherits the 'id' attribute from IEntity.

    Example
    -------
    >>> class Team(IRootEntity):
    ...     def __init__(self, id, members):
    ...         self._id = id
    ...         self.members = members
    ...     @property
    ...     def id(self):
    ...         return self._id
    >>> team1 = Team(1, ['Alice', 'Bob'])
    >>> team2 = Team(2, ['Charlie', 'David'])
    >>> team1 == team2
    False
    """


class IValueObject(abc.ABC):
    """
    An abstract base class that represents a Value Object in Domain-Driven Design.

    In Domain-Driven Design (DDD), a Value Object is an object that is defined by its
    value or attributes, rather than a unique identity. Value Objects are often used
    to describe aspects of a domain and are typically immutable.

    This class prohibits setting of attributes post-instantiation to enforce
    immutability, and defines equality in terms of attribute equality.

    Methods
    -------
    __setattr__(__name, __value) -> None:
        Overridden to prohibit attribute mutation.

    __eq__(value_object) -> bool:
        Abstract method to check equality of Value Objects.

    Example
    -------
    >>> class Color(IValueObject):
    ...     def __init__(self, red, green, blue):
    ...         object.__setattr__(self, 'red', red)
    ...         object.__setattr__(self, 'green', green)
    ...         object.__setattr__(self, 'blue', blue)
    ...
    ...     def __eq__(self, color):
    ...         return (isinstance(color, Color) and
    ...                 self.red == color.red and
    ...                 self.green == color.green and
    ...                 self.blue == color.blue)
    >>> color1 = Color(255, 255, 255)
    >>> color2 = Color(255, 255, 255)
    >>> color1 == color2
    True
    """

    def __setattr__(self, __name, __value) -> None:
        """
        Prohibit attribute mutation after instantiation.

        Parameters
        ----------
        __name : str
            The name of the attribute to set.
        __value : Any
            The value to set.

        Raises
        ------
        NotImplementedError
            Always, because value objects are immutable.
        """
        raise NotImplementedError("Value objects are immutable.")

    @abc.abstractmethod
    def __eq__(self, value_object):
        """
        Abstract method for comparing two value objects based on their attributes.

        Parameters
        ----------
        value_object : IValueObject
            The value object to compare with.

        Raises
        ------
        AssertionError
            If `value_object` is not an instance of `IValueObject`.
        """
        assert isinstance(value_object, IValueObject), type_error_msg(value_object, IValueObject)
        raise NotImplementedError()


class ILoadRepository(IService):
    """
    An abstract base class for repositories that load entities from the underlying
    data storage.

    In Domain-Driven Design (DDD), a Repository is a mechanism for encapsulating
    storage, retrieval, and search behavior, allowing the rest of the application
    to remain agnostic to the data persistence layer. A repository that implements
    this interface is responsible for retrieving Aggregate Roots from the data storage.

    Methods
    -------
    load(*args, **kwargs) -> IRootEntity:
        Abstract method that should load an Aggregate Root.

    Example
    -------
    >>> class User:
    ...     def __init__(self, id, name):
    ...         self.id = id
    ...         self.name = name
    ...
    >>> class UserRepository(ILoadRepository):
    ...     def __init__(self, users):
    ...         self.users = {user.id: user for user in users}
    ...     def load(self, user_id):
    ...         return self.users.get(user_id)
    >>> alice = User(1, 'Alice')
    >>> bob = User(2, 'Bob')
    >>> repo = UserRepository([alice, bob])
    >>> print(repo.load(1) == alice)
    True
    """

    @abc.abstractmethod
    def load(self, *args, **kwargs):
        """
        Abstract method that when implemented should load an Aggregate Root from
        the data storage.

        Parameters
        ----------
        *args
            Variable length argument list.
        **kwargs
            Arbitrary keyword arguments.

        Returns
        -------
        IRootEntity
            The loaded Aggregate Root.
        """
        raise NotImplementedError()


class ISaveRepository(IService):
    """
    An abstract base class for repositories that save entities to the underlying
    data storage.

    In Domain-Driven Design (DDD), a Repository is a mechanism for encapsulating
    storage, retrieval, and search behavior, allowing the rest of the application
    to remain agnostic to the data persistence layer. A repository that implements
    this interface is responsible for persisting Aggregate Roots to the data storage.

    Methods
    -------
    save(entity: IRootEntity):
        Abstract method that should save an Aggregate Root.

    Example
    -------
    >>> class User:
    ...     def __init__(self, id, name):
    ...         self.id = id
    ...         self.name = name
    ...
    >>> class UserRepository(ISaveRepository):
    ...     def __init__(self):
    ...         self.users = {}
    ...     def save(self, user):
    ...         self.users[user.id] = user
    >>> alice = User(1, 'Alice')
    >>> repo = UserRepository()
    >>> repo.save(alice)
    >>> print(repo.users == {1: alice})
    True
    """

    @abc.abstractmethod
    def save(self, entity):
        """
        Abstract method that when implemented should save an Aggregate Root to
        the data storage.

        Parameters
        ----------
        entity : IRootEntity
            The Aggregate Root to save.
        """
        raise NotImplementedError()


class IRepository(ILoadRepository, ISaveRepository):
    """
    An abstract base class for repositories that handle both loading and saving
    of Aggregate Roots to and from the underlying data storage.

    In Domain-Driven Design (DDD), a Repository is a mechanism for encapsulating
    storage, retrieval, and search behavior, allowing the rest of the application
    to remain agnostic to the data persistence layer. A repository that implements
    this interface is responsible for both retrieving and persisting Aggregate Roots
    to and from the data storage.

    This class combines the interfaces of ILoadRepository and ISaveRepository, providing
    a unified interface for repositories that handle both operations.

    Example
    -------
    >>> class User:
    ...     def __init__(self, id, name):
    ...         self.id = id
    ...         self.name = name
    ...
    >>> class UserRepository(IRepository):
    ...     def __init__(self):
    ...         self.users = {}
    ...     def load(self, user_id):
    ...         return self.users.get(user_id)
    ...     def save(self, user):
    ...         self.users[user.id] = user
    >>> alice = User(1, 'Alice')
    >>> repo = UserRepository()
    >>> repo.save(alice)
    >>> print(repo.load(1) == alice)
    True
    """


class IFactory(abc.ABC):
    """
    An abstract base class that represents a Factory in Domain-Driven Design.

    In Domain-Driven Design (DDD), a Factory is used to handle the creation of complex
    objects, aggregates, or value objects. This Factory interface encapsulates the
    knowledge of creating these complex domain objects, isolating the rest of the
    application from this complexity.

    The Factory interface would typically have one or more creation methods, which
    would be responsible for creating and returning an instance of a complex object.

    Example
    -------
    >>> class User:
    ...     def __init__(self, id, name):
    ...         self.id = id
    ...         self.name = name
    ...
    >>> class UserFactory(IFactory):
    ...     def create(self, id, name):
    ...         return User(id, name)
    ...
    >>> factory = UserFactory()
    >>> alice = factory.create(1, 'Alice')
    >>> print(alice.name)
    Alice
    """


class IBuilder(IFactory):
    """
    An abstract base class that represents a Builder in Domain-Driven Design.

    In Domain-Driven Design (DDD), a Builder is used to construct complex objects step
    by step. It's the same construction process to create different types of products.
    This Builder interface encapsulates the process of building complex domain objects,
    isolating the rest of the application from this complexity.

    The Builder interface typically provides methods to configure the product before
    calling the build method to generate the final product.

    It inherits the create method from the IFactory class.

    Methods
    -------
    build():
        Abstract method to finalize and return the constructed product.

    Example
    -------
    >>> class User:
    ...     def __init__(self):
    ...         self.id = None
    ...         self.name = None
    ...
    >>> class UserBuilder(IBuilder):
    ...     def __init__(self):
    ...         self._user = User()
    ...     def id(self, id):
    ...         self._user.id = id
    ...         return self
    ...     def name(self, name):
    ...         self._user.name = name
    ...         return self
    ...     def build(self):
    ...         return self._user
    >>> builder = UserBuilder()
    >>> alice = builder.id(1).name('Alice').build()
    >>> print(alice.name)
    Alice
    """

    @abc.abstractmethod
    def build(self):
        """
        Abstract method that when implemented should finalize and return the constructed
        product.

        Returns
        -------
        object
            The finalized constructed product.
        """
        raise NotImplementedError()


class IDomainService(IService):
    """
    An abstract base class representing a Domain Service in Domain-Driven Design.

    In Domain-Driven Design (DDD), a Domain Service is a stateless service that
    performs operations related to a domain concept that don't naturally belong
    to an Entity or Value Object. These operations can involve multiple Entities
    or Value Objects and can encapsulate complex business logic or calculations.

    The Domain Service should not be confused with Application Services or
    Infrastructure Services, which serve different purposes within the layered
    architecture of a DDD application.

    Example
    -------
    >>> class UserRegistrationService(IDomainService):
    ...     def register_user(self, username, password):
    ...         # Validate input, create a User, and save to the UserRepository
    ...         pass
    """


@unique
class DomainEventType(Enum):
    """
    An enumeration of domain event types.

    Each event type in the system should have a corresponding value in this enumeration.
    """


class IDomainEvent(abc.ABC):
    """
    An abstract base class representing a Domain Event in Domain-Driven Design.

    In Domain-Driven Design (DDD), a Domain Event is a message or signal that is
    generated by the domain model when something notable happens that affects the
    domain. Domain Events are a way of capturing the temporal aspects of the domain,
    allowing us to model what happens over time, not just the state of the domain at
    a particular moment.

    Domain Events typically carry the data that describes what happened and a type
    or name that indicates what kind of event occurred. This data should be sufficient
    for other parts of the system to react appropriately to the event, without needing
    to know the details of what caused the event.

    Properties
    ----------
    type : str
        The type or name of the event.

    Example
    -------
    >>> class UserRegisteredEvent(IDomainEvent):
    ...     @property
    ...     def type(self):
    ...         return 'UserRegisteredEvent'
    ...
    ...     def __init__(self, user_id, username):
    ...         self.user_id = user_id
    ...         self.username = username
    """

    @abc.abstractproperty
    def type(self):
        """
        Abstract property that should return the type or name of the event.

        Returns
        -------
        DomainEventType
            The type of the event.
        """
        raise NotImplementedError()


class IApplicationService(IService):
    """
    An interface for an application service in a software system following
    Domain-Driven Design (DDD).

    An application service is responsible for orchestrating tasks, managing
    transactions, and coordinating responses within the application layer of
    the system. They don't contain business rules or knowledge, but control
    how domain objects (entities) are used to perform operations related to
    application-level concerns such as user interface processing, task-based
    UI, and security.

    Example
    -------
    >>> class UserService(IApplicationService):
    ...     def register_new_user(self, username, password):
    ...         # Register a new user using domain objects
    ...         pass
    """


class IInfrastructureService(IService):
    """
    An interface for an infrastructure service in a software system following
    Domain-Driven Design (DDD).

    An infrastructure service provides technical capabilities supporting the layers
    of the application, such as messaging, persistence, UI components, etc. This
    type of service essentially bridges the gap between the domain model and
    technical services.

    Example
    -------
    >>> class EmailService(IInfrastructureService):
    ...     def send_email(self, recipient, subject, body):
    ...         # Send an email using some email infrastructure
    ...         pass
    """


class IUnitOfWork(abc.ABC):
    """
    An interface for the Unit of Work pattern in a software system following
    Domain-Driven Design (DDD).

    The Unit of Work pattern manages transactions, helps to ensure consistency
    when dealing with multiple aggregates, and enables efficient in-memory
    transactional behavior.

    Example
    -------
    >>> class SqlAlchemyUnitOfWork(IUnitOfWork):
    ...     def __init__(self, session_factory):
    ...         self.session_factory = session_factory
    ...
    ...     def __enter__(self):
    ...         self.session = self.session_factory()  # Open a new session
    ...         return self
    ...
    ...     def __exit__(self, *args):
    ...         self.session.close()  # Close the session
    """


class ISpecification(abc.ABC):
    """
    An interface for the Specification pattern in a software system following
    Domain-Driven Design (DDD).

    The Specification pattern encapsulates complex business rules that can be
    re-used across the system, and provides a way to combine those rules using
    boolean logic operations.

    Example
    -------
    >>> class UserActiveSpecification(ISpecification):
    ...     def is_satisfied_by(self, user):
    ...         # Check if the user is active
    ...         return user.is_active
    """


class IPolicy(abc.ABC):
    """
    An interface for defining policies in a software system following
    Domain-Driven Design (DDD).

    Policies are similar to services but typically embody less complex
    domain rules and are used to offload domain logic from entities.

    Example
    -------
    >>> class UserPromotionPolicy(IPolicy):
    ...     def apply(self, user):
    ...         # Promote the user if they meet certain criteria
    ...         pass
    """


class ICommandHandler(abc.ABC):
    """
    An interface for handling commands in a software system following
    Domain-Driven Design (DDD) and Command Query Responsibility Segregation
    (CQRS) pattern.

    Command handlers encapsulate the operation or business logic that is performed
    when a particular command is issued to the system.

    Example
    -------
    >>> class RegisterUserCommandHandler(ICommandHandler):
    ...     def handle(self, command):
    ...         # Handle the 'RegisterUser' command
    ...         pass
    """


class IQueryHandler(abc.ABC):
    """
    An interface for handling queries in a software system following
    Domain-Driven Design (DDD) and Command Query Responsibility Segregation
    (CQRS) pattern.

    Query handlers encapsulate the logic for fetching data from the system
    in response to a particular query.

    Example
    -------
    >>> class GetUserQueryHandler(IQueryHandler):
    ...     def handle(self, query):
    ...         # Handle the 'GetUser' query
    ...         pass
    """
