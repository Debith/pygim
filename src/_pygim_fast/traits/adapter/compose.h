// traits/adapter/compose.h — compose(): a trait's members copied into a class, rewritten to belong there;
// traits_of(): where each composed member came from.
//
// Worked example:
//
//   class Greeter:                                     # a trait: nothing inherits from it
//       def greet(self): return f"hello {self.__name}" # reads _Greeter__name
//   class Person:
//       def __init__(self, name): self.__name = name   # stores _Person__name
//
//   compose(Person, Greeter)            -> Person, with greet in Person's own namespace
//   Person("ann").greet()               -> 'hello ann'          the private name was respelt
//   Person.greet.__qualname__           -> 'Person.greet'
//   traits_of(Person)["greet"].trait    -> '<module>.Greeter'
//   compose(Person, Greeter)            -> ValueError: Person already has greet; ... (pass override=True)
//
// Two passes, so a refusal writes nothing: every member of every trait is classified, checked and
// rewritten first, and only then is anything set on the target. A name is a conflict when the
// target's OWN namespace holds it, or when two traits of one call both define it; an inherited name
// is not (every class inherits object.__lt__), and composing over it is what defining it would do.
//
// The kinds of member are classes, each with its own behaviour, held by Member (a std::variant,
// pygim's #134): a kind says whether it is one, how it is rewritten and which code object says where
// it was defined. A member no kind takes is refused by name — unless it is a dunder, which is the
// trait class's own machinery (__module__, __doc__, __dict__) and stays behind.
//
// Composing a class that was itself composed into carries its record along: each member keeps the
// trait it first came from and where that trait defined it; only `composed` names the new call. A
// member the class defined itself — or replaced since it was composed — is attributed to that class.
// The record is accurate: it lists only the members the class still holds as compose() left them.
//
// Where each part lives: composer.h (Composer), trait_move.h (TraitMove), member.h (the kinds of
// member), plan.h (PlannedMember, Clash, Plan), function_copy.h, record.h (Provenance, TraitRecord,
// the side table), introspection.h (what is read from Python objects).
//
// Lifetime and threading: a class's record is kept off the class, in a side table keyed by the class's
// identity, and removed when the class is collected (detail::RecordStore); a class composed into gets a
// record of its own, never its base's. compose() holds the GIL throughout, as every pybind11 call does.

#pragma once

#include <pybind11/pybind11.h>

#include "composer.h"
#include "introspection.h"
#include "record.h"

namespace pygim::traits {

namespace py = pybind11;

/// Copy every member of every trait into `target`, rewritten to belong there, and record where each
/// came from. Refuses — writing nothing — a trait that is not a class, a member no kind composes,
/// a name `target` already has, or a name two traits define; `override=True` replaces instead (the
/// later trait winning). Returns `target`.
/// Cost: per member, one CodeBuilder build per function in it; per call, one frame lookup.
/// Example: compose(Person, Greeter) is Person, and Person("ann").greet() is 'hello ann'.
inline py::type compose(const py::type& target, const py::args& traits, bool override_existing) {
    detail::Composer composer(target);
    for (const auto& trait : traits) composer.add(trait);
    return composer.override(override_existing).compose();
}

/// The record of what was composed into `cls` itself and is still there: a member replaced or
/// deleted since is left out (D15). Empty for a class nothing was composed into.
/// Example: list(traits_of(Person)) is ['greet'].
inline py::object traits_of(const py::type& cls) {
    const TraitRecord* found = detail::records().find(cls);
    // A snapshot: the store stays private.
    return py::cast(found != nullptr ? found->standing_in(detail::own_namespace(cls)) : TraitRecord{});
}

}  // namespace pygim::traits
