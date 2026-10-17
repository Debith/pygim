<!-- Generated from tests/unittests/traits_cases.py by `python tests/unittests/traits_cases.py`;
     tests/unittests/test_traits_cases_page.py fails when it is not current. Edit the cases, not this page. -->

# pygim.traits and Python's naming rules: the test cases

Each row is one call: its arguments, then what it is expected to give, then the case's id. `—` is an argument not passed or a result a refused call does not have; `raises T: text` is the exception expected, with a fragment of its message. Expected values come from the contracts — Python's own rules and the refusals [traits.md](traits.md) documents — never from what the code returned (global #118).

"Change one input" shows the rows that differ from each other in one argument only, the others fixed as stated above each table; "Every case" shows them all, interactions included.

## pygim.utils.is_dunder

`is_dunder(name)`

The expected results are Python's own rule, enum._is_dunder; the test also checks every row of this table against enum._is_dunder itself.

### Change one input

#### Vary `name`

Fixed: .

| name | expected_result | case |
| --- | --- | --- |
| `'__ab__'` | `True` | baseline |
| `'_ab__'` | `False` | leading_run_one |
| `'___ab__'` | `False` | leading_run_three |
| `'__ab_'` | `False` | trailing_run_one |
| `'__ab___'` | `False` | trailing_run_three |
| `'____'` | `False` | body_empty |
| `'__a__'` | `True` | body_one_character |
| `'__a_b__'` | `True` | body_with_underscore |
| `'__a__b__'` | `True` | body_with_double_underscore |
| `'__lt__'` | `True` | protocol_method |
| `'__init__'` | `True` | initialiser |
| `'__module__'` | `True` | class_machinery |
| `''` | `False` | empty |
| `'_'` | `False` | one_underscore |
| `'__'` | `False` | two_underscores |
| `'___'` | `False` | three_underscores |
| `'_____'` | `False` | five_underscores |
| `'__x'` | `False` | private_name |
| `'__x_'` | `False` | private_name_trailing_one |
| `'x__'` | `False` | trailing_only |
| `'_x_'` | `False` | single_underscores |
| `'_Trait__x'` | `False` | mangled_name |
| `'x'` | `False` | plain |
| `None` | `raises TypeError` | not_a_string |

### Every case

| name | expected_result | case |
| --- | --- | --- |
| `'__ab__'` | `True` | baseline |
| `'_ab__'` | `False` | leading_run_one |
| `'___ab__'` | `False` | leading_run_three |
| `'__ab_'` | `False` | trailing_run_one |
| `'__ab___'` | `False` | trailing_run_three |
| `'____'` | `False` | body_empty |
| `'__a__'` | `True` | body_one_character |
| `'__a_b__'` | `True` | body_with_underscore |
| `'__a__b__'` | `True` | body_with_double_underscore |
| `'__lt__'` | `True` | protocol_method |
| `'__init__'` | `True` | initialiser |
| `'__module__'` | `True` | class_machinery |
| `''` | `False` | empty |
| `'_'` | `False` | one_underscore |
| `'__'` | `False` | two_underscores |
| `'___'` | `False` | three_underscores |
| `'_____'` | `False` | five_underscores |
| `'__x'` | `False` | private_name |
| `'__x_'` | `False` | private_name_trailing_one |
| `'x__'` | `False` | trailing_only |
| `'_x_'` | `False` | single_underscores |
| `'_Trait__x'` | `False` | mangled_name |
| `'x'` | `False` | plain |
| `None` | `raises TypeError` | not_a_string |

## pygim.traits.CodeBuilder

`CodeBuilder(code).reown(steps[0].source, steps[0].target).reown(steps[1]...).build()`

`code` is the named function's code object; `result.inner` is the code object nested in its constants (a lambda); `original.co_names` is the code passed in, after build() — a code object is never changed.

co_qualname exists from Python 3.11; on 3.9 and 3.10 the qualified-name columns are not checked.

Besides these literal expectations, every row but `source_not_the_owner` and `other_class_private_chained` is checked against Python's own compiler: the method's source compiled inside a class named like the last target must give the same names.

### Change one input

#### Vary `code`

Fixed: `steps[0].source` = `Trait`; `steps[0].target` = `Target`; `steps[1].source` = —; `steps[1].target` = —.

| code | result.co_names | result.co_qualname | result.inner.co_names | result.inner.co_qualname | original.co_names | case |
| --- | --- | --- | --- | --- | --- | --- |
| `Trait.plain` | `('_Target__secret',)` | `'Target.plain'` | — | — | `('_Trait__secret',)` | baseline |
| `Trait.public` | `('value',)` | `'Target.public'` | — | — | `('value',)` | code_public_name |
| `Trait.dunder` | `('__class__', '__name__')` | `'Target.dunder'` | — | — | `('__class__', '__name__')` | code_dunder_names |
| `Trait.nested` | `()` | `'Target.nested'` | `('_Target__secret',)` | `'Target.nested.<locals>.<lambda>'` | `()` | code_nested |
| `'return 1'` | `raises TypeError: CodeBuilder takes a code object, not str` | — | — | — | — | code_not_code |

#### Vary `steps[0].source`

Fixed: `code` = `Trait.plain`; `steps[0].target` = `Target`; `steps[1].source` = —; `steps[1].target` = —.

| steps[0].source | result.co_names | result.co_qualname | result.inner.co_names | result.inner.co_qualname | original.co_names | case |
| --- | --- | --- | --- | --- | --- | --- |
| `Trait` | `('_Target__secret',)` | `'Target.plain'` | — | — | `('_Trait__secret',)` | baseline |
| `Other` | `('_Trait__secret',)` | `'Trait.plain'` | — | — | `('_Trait__secret',)` | source_not_the_owner |
| `'Trait'` | `raises TypeError` | — | — | — | — | source_not_a_class |

#### Vary `steps[0].target`

Fixed: `code` = `Trait.plain`; `steps[0].source` = `Trait`; `steps[1].source` = —; `steps[1].target` = —.

| steps[0].target | result.co_names | result.co_qualname | result.inner.co_names | result.inner.co_qualname | original.co_names | case |
| --- | --- | --- | --- | --- | --- | --- |
| `Target` | `('_Target__secret',)` | `'Target.plain'` | — | — | `('_Trait__secret',)` | baseline |
| `__Target` | `('_Target__secret',)` | `'__Target.plain'` | — | — | `('_Trait__secret',)` | target_leading_underscores |
| `_` | `('__secret',)` | `'_.plain'` | — | — | `('_Trait__secret',)` | target_only_underscores |
| `'Target'` | `raises TypeError` | — | — | — | — | target_not_a_class |

#### Vary `code`

Fixed: `steps[0].source` = `Trait`; `steps[0].target` = `First`; `steps[1].source` = `First`; `steps[1].target` = `Second`.

| code | result.co_names | result.co_qualname | result.inner.co_names | result.inner.co_qualname | original.co_names | case |
| --- | --- | --- | --- | --- | --- | --- |
| `Trait.plain` | `('_Second__secret',)` | `'Second.plain'` | — | — | `('_Trait__secret',)` | two_steps |
| `Trait.others_private` | `('_Second__x',)` | `'Second.others_private'` | — | — | `('_First__x',)` | other_class_private_chained |

### Every case

| code | steps[0].source | steps[0].target | steps[1].source | steps[1].target | result.co_names | result.co_qualname | result.inner.co_names | result.inner.co_qualname | original.co_names | case |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `Trait.plain` | `Trait` | `Target` | — | — | `('_Target__secret',)` | `'Target.plain'` | — | — | `('_Trait__secret',)` | baseline |
| `Trait.plain` | `Trait` | `__Target` | — | — | `('_Target__secret',)` | `'__Target.plain'` | — | — | `('_Trait__secret',)` | target_leading_underscores |
| `Trait.plain` | `Trait` | `_` | — | — | `('__secret',)` | `'_.plain'` | — | — | `('_Trait__secret',)` | target_only_underscores |
| `Trait.plain` | `Other` | `Target` | — | — | `('_Trait__secret',)` | `'Trait.plain'` | — | — | `('_Trait__secret',)` | source_not_the_owner |
| `Trait.public` | `Trait` | `Target` | — | — | `('value',)` | `'Target.public'` | — | — | `('value',)` | code_public_name |
| `Trait.dunder` | `Trait` | `Target` | — | — | `('__class__', '__name__')` | `'Target.dunder'` | — | — | `('__class__', '__name__')` | code_dunder_names |
| `Trait.nested` | `Trait` | `Target` | — | — | `()` | `'Target.nested'` | `('_Target__secret',)` | `'Target.nested.<locals>.<lambda>'` | `()` | code_nested |
| `Trait.plain` | — | — | — | — | `('_Trait__secret',)` | `'Trait.plain'` | — | — | `('_Trait__secret',)` | no_steps |
| `Trait.plain` | `Trait` | `First` | `First` | `Second` | `('_Second__secret',)` | `'Second.plain'` | — | — | `('_Trait__secret',)` | two_steps |
| `_Trait.plain` | `_Trait` | `Target` | — | — | `('_Target__secret',)` | `'Target.plain'` | — | — | `('_Trait__secret',)` | owner_leading_underscore |
| `_.plain` | `_` | `Target` | — | — | `('_Target__secret',)` | `'Target.plain'` | — | — | `('__secret',)` | owner_only_underscores |
| `Trait.others_private` | `Trait` | `Second` | — | — | `('_First__x',)` | `'Second.others_private'` | — | — | `('_First__x',)` | other_class_private_direct |
| `Trait.others_private` | `Trait` | `First` | `First` | `Second` | `('_Second__x',)` | `'Second.others_private'` | — | — | `('_First__x',)` | other_class_private_chained |
| `'return 1'` | `Trait` | `Target` | — | — | `raises TypeError: CodeBuilder takes a code object, not str` | — | — | — | — | code_not_code |
| `Trait.plain` | `'Trait'` | `Target` | — | — | `raises TypeError` | — | — | — | — | source_not_a_class |
| `Trait.plain` | `Trait` | `'Target'` | — | — | `raises TypeError` | — | — | — | — | target_not_a_class |

## pygim.traits.compose

`compose(target, traits[0], traits[1], override=override)`

`effect: written` is the names the call set in the target's own namespace, its record `__pygim_traits__` aside; a refused call writes nothing. `effect: traits_of` is list(traits_of(target)) after the call.

### Change one input

#### Vary `target`

Fixed: `traits[0]` = `Greeter`; `traits[1]` = —; `override` = `False`.

| target | expected_result | effect: written | effect: traits_of | case |
| --- | --- | --- | --- | --- |
| `Person` | `the target` | `('greet', 'kind', 'name_length', 'shout', 'version')` | `('greet', 'kind', 'name_length', 'shout', 'version')` | baseline |
| `Person with shout` | `raises ValueError: Person already has shout; traits_cases.Greeter would replace it (pass override=True)` | `()` | `()` | target_has_the_name |
| `'Person'` | `raises TypeError` | — | — | target_not_a_class |

#### Vary `traits[0]`

Fixed: `target` = `Person`; `traits[1]` = —; `override` = `False`.

| traits[0] | expected_result | effect: written | effect: traits_of | case |
| --- | --- | --- | --- | --- |
| `Greeter` | `the target` | `('greet', 'kind', 'name_length', 'shout', 'version')` | `('greet', 'kind', 'name_length', 'shout', 'version')` | baseline |
| `Loud` | `the target` | `('shout',)` | `('shout',)` | trait_one_function |
| `Ordered` | `the target` | `('__lt__',)` | `('__lt__',)` | trait_name_the_target_inherits |
| `Machinery` | `the target` | `()` | `()` | trait_only_machinery |
| `WithConstant` | `raises TypeError: WithConstant.factor is int; a trait composes functions, properties, classmethods and staticmethods` | `()` | `()` | trait_with_a_constant |
| `not_a_trait` | `raises TypeError: compose takes classes as traits, not function` | `()` | `()` | trait_not_a_class |
| — | `the target` | `()` | `()` | no_traits |

#### Vary `traits[1]`

Fixed: `target` = `Person`; `traits[0]` = `Greeter`; `override` = `False`.

| traits[1] | expected_result | effect: written | effect: traits_of | case |
| --- | --- | --- | --- | --- |
| — | `the target` | `('greet', 'kind', 'name_length', 'shout', 'version')` | `('greet', 'kind', 'name_length', 'shout', 'version')` | baseline |
| `Loud` | `raises ValueError: traits_cases.Greeter and traits_cases.Loud both define shout (pass override=True to let the later win)` | `()` | `()` | two_traits_one_name |

#### Vary `override`

Fixed: `target` = `Person with shout`; `traits[0]` = `Greeter`; `traits[1]` = —.

| override | expected_result | effect: written | effect: traits_of | case |
| --- | --- | --- | --- | --- |
| `False` | `raises ValueError: Person already has shout; traits_cases.Greeter would replace it (pass override=True)` | `()` | `()` | target_has_the_name |
| `True` | `the target` | `('greet', 'kind', 'name_length', 'shout', 'version')` | `('greet', 'kind', 'name_length', 'shout', 'version')` | target_has_the_name_override |

#### Vary `traits[1]`

Fixed: `target` = `Person with shout`; `traits[0]` = `Greeter`; `override` = `True`.

| traits[1] | expected_result | effect: written | effect: traits_of | case |
| --- | --- | --- | --- | --- |
| — | `the target` | `('greet', 'kind', 'name_length', 'shout', 'version')` | `('greet', 'kind', 'name_length', 'shout', 'version')` | target_has_the_name_override |
| `Loud` | `the target` | `('greet', 'kind', 'name_length', 'shout', 'version')` | `('greet', 'kind', 'name_length', 'shout', 'version')` | target_and_two_traits_override |

#### Vary `override`

Fixed: `target` = `Person`; `traits[0]` = `Greeter`; `traits[1]` = `Loud`.

| override | expected_result | effect: written | effect: traits_of | case |
| --- | --- | --- | --- | --- |
| `False` | `raises ValueError: traits_cases.Greeter and traits_cases.Loud both define shout (pass override=True to let the later win)` | `()` | `()` | two_traits_one_name |
| `True` | `the target` | `('greet', 'kind', 'name_length', 'shout', 'version')` | `('greet', 'kind', 'name_length', 'shout', 'version')` | two_traits_one_name_override |

#### Vary `target`

Fixed: `traits[0]` = `Greeter`; `traits[1]` = `Loud`; `override` = `True`.

| target | expected_result | effect: written | effect: traits_of | case |
| --- | --- | --- | --- | --- |
| `Person` | `the target` | `('greet', 'kind', 'name_length', 'shout', 'version')` | `('greet', 'kind', 'name_length', 'shout', 'version')` | two_traits_one_name_override |
| `Person with shout` | `the target` | `('greet', 'kind', 'name_length', 'shout', 'version')` | `('greet', 'kind', 'name_length', 'shout', 'version')` | target_and_two_traits_override |

### Every case

| target | traits[0] | traits[1] | override | expected_result | effect: written | effect: traits_of | case |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `Person` | `Greeter` | — | `False` | `the target` | `('greet', 'kind', 'name_length', 'shout', 'version')` | `('greet', 'kind', 'name_length', 'shout', 'version')` | baseline |
| `Person` | `Loud` | — | `False` | `the target` | `('shout',)` | `('shout',)` | trait_one_function |
| `Person` | `Ordered` | — | `False` | `the target` | `('__lt__',)` | `('__lt__',)` | trait_name_the_target_inherits |
| `Person` | `Machinery` | — | `False` | `the target` | `()` | `()` | trait_only_machinery |
| `Person` | `WithConstant` | — | `False` | `raises TypeError: WithConstant.factor is int; a trait composes functions, properties, classmethods and staticmethods` | `()` | `()` | trait_with_a_constant |
| `Person` | `not_a_trait` | — | `False` | `raises TypeError: compose takes classes as traits, not function` | `()` | `()` | trait_not_a_class |
| `Person` | — | — | `False` | `the target` | `()` | `()` | no_traits |
| `Person with shout` | `Greeter` | — | `False` | `raises ValueError: Person already has shout; traits_cases.Greeter would replace it (pass override=True)` | `()` | `()` | target_has_the_name |
| `Person with shout` | `Greeter` | — | `True` | `the target` | `('greet', 'kind', 'name_length', 'shout', 'version')` | `('greet', 'kind', 'name_length', 'shout', 'version')` | target_has_the_name_override |
| `Person` | `Greeter` | `Loud` | `False` | `raises ValueError: traits_cases.Greeter and traits_cases.Loud both define shout (pass override=True to let the later win)` | `()` | `()` | two_traits_one_name |
| `Person` | `Greeter` | `Loud` | `True` | `the target` | `('greet', 'kind', 'name_length', 'shout', 'version')` | `('greet', 'kind', 'name_length', 'shout', 'version')` | two_traits_one_name_override |
| `Person with shout` | `Greeter` | `Loud` | `True` | `the target` | `('greet', 'kind', 'name_length', 'shout', 'version')` | `('greet', 'kind', 'name_length', 'shout', 'version')` | target_and_two_traits_override |
| `Child(Base)` | `Polite` | — | `False` | `the target` | `('hello',)` | `('hello',)` | target_with_a_base |
| `Point` | `Doubled` | — | `False` | `the target` | `('doubled',)` | `('doubled',)` | target_frozen_slots_dataclass |
| `Colour` | `Shouted` | — | `False` | `the target` | `('shouted',)` | `('shouted',)` | target_enum |
| `'Person'` | `Greeter` | — | `False` | `raises TypeError` | — | — | target_not_a_class |

## pygim.traits.compose: the composed members, called

`compose(target, traits[0], traits[1], override=override), then instance.<member>`

`member` is what is read on an instance of the target: `name()` calls it, `name(other)` calls it with a second instance, a dotted name reads attributes.

### Change one input

#### Vary `member`

Fixed: `target` = `Person`; `traits[0]` = `Greeter`; `traits[1]` = —; `override` = `False`.

| member | expected_result | case |
| --- | --- | --- |
| `'greet()'` | `'hello ann'` | baseline |
| `'shout()'` | `'ANN'` | member_with_nested_code |
| `'name_length'` | `3` | member_property |
| `'kind()'` | `'Person'` | member_classmethod |
| `'version()'` | `1` | member_staticmethod |
| `'greet.__qualname__'` | `'Person.greet'` | member_qualified_name |

### Every case

| target | traits[0] | traits[1] | override | member | expected_result | case |
| --- | --- | --- | --- | --- | --- | --- |
| `Person` | `Greeter` | — | `False` | `'greet()'` | `'hello ann'` | baseline |
| `Person` | `Greeter` | — | `False` | `'shout()'` | `'ANN'` | member_with_nested_code |
| `Person` | `Greeter` | — | `False` | `'name_length'` | `3` | member_property |
| `Person` | `Greeter` | — | `False` | `'kind()'` | `'Person'` | member_classmethod |
| `Person` | `Greeter` | — | `False` | `'version()'` | `1` | member_staticmethod |
| `Person` | `Greeter` | — | `False` | `'greet.__qualname__'` | `'Person.greet'` | member_qualified_name |
| `Person with shout` | `Greeter` | — | `True` | `'shout()'` | `'ANN'` | override_replaces_the_targets |
| `Person` | `Greeter` | `Loud` | `True` | `'shout()'` | `'LOUD'` | override_the_later_trait_wins |
| `Person` | `Ordered` | — | `False` | `'__lt__(other)'` | `True` | protocol_method |
| `Child(Base)` | `Polite` | — | `False` | `'hello()'` | `'polite+base'` | super_resolves_in_the_target |
| `Point` | `Doubled` | — | `False` | `'doubled()'` | `8` | frozen_slots_dataclass |
| `Colour` | `Shouted` | — | `False` | `'shouted()'` | `'RED'` | enum_member |

## pygim.traits.traits_of

`compose(target, traits[0], traits[1], override=override); traits_of(target)[name]`

`expected_result.defined` names the trait member; its expected value is `file:line` of the code that member is made of. `expected_result.composed` is `file:line` of the compose() call.

### Change one input

#### Vary `traits[0]`

Fixed: `target` = `Person`; `traits[1]` = —; `override` = `False`; `name` = `'greet'`.

| traits[0] | expected_result.trait | expected_result.defined | expected_result.composed | case |
| --- | --- | --- | --- | --- |
| `Greeter` | `'traits_cases.Greeter'` | `Greeter.greet` | `the compose() call` | baseline |
| — | `raises KeyError` | — | — | nothing_composed |

#### Vary `name`

Fixed: `target` = `Person`; `traits[0]` = `Greeter`; `traits[1]` = —; `override` = `False`.

| name | expected_result.trait | expected_result.defined | expected_result.composed | case |
| --- | --- | --- | --- | --- |
| `'greet'` | `'traits_cases.Greeter'` | `Greeter.greet` | `the compose() call` | baseline |
| `'name_length'` | `'traits_cases.Greeter'` | `Greeter.name_length` | `the compose() call` | name_property |
| `'kind'` | `'traits_cases.Greeter'` | `Greeter.kind` | `the compose() call` | name_classmethod |
| `'missing'` | `raises KeyError` | — | — | name_not_composed |

### Every case

| target | traits[0] | traits[1] | override | name | expected_result.trait | expected_result.defined | expected_result.composed | case |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `Person` | `Greeter` | — | `False` | `'greet'` | `'traits_cases.Greeter'` | `Greeter.greet` | `the compose() call` | baseline |
| `Person` | `Greeter` | — | `False` | `'name_length'` | `'traits_cases.Greeter'` | `Greeter.name_length` | `the compose() call` | name_property |
| `Person` | `Greeter` | — | `False` | `'kind'` | `'traits_cases.Greeter'` | `Greeter.kind` | `the compose() call` | name_classmethod |
| `Person` | `Greeter` | — | `False` | `'missing'` | `raises KeyError` | — | — | name_not_composed |
| `Person` | `Greeter` | `Loud` | `True` | `'shout'` | `'traits_cases.Loud'` | `Loud.shout` | `the compose() call` | later_trait_recorded |
| `Person` | — | — | `False` | `'greet'` | `raises KeyError` | — | — | nothing_composed |

## What these cases do not cover

- Members a trait may hold are functions, properties, classmethods and staticmethods; a trait member written in C++ (the quantity traits to come) has no case yet.
- Zero-argument super() is covered for a function member; not inside a property or a classmethod.
- A keyword argument named with two leading underscores in a call is not mangled by Python, so CodeBuilder has nothing to do for it; no row shows that.
- Composing into a class with a custom metaclass other than Enum's, into a pybind11 class, and from several threads at once is not covered.
- On Python 3.9 the rows whose target is Point, a frozen slots dataclass, are skipped: dataclass(slots=True) is Python 3.10's.
- One-input groups explain one input's effect; they do not replace the interaction rows, and no table is the full product of its columns.
- The C++ core (utils/python_names.h, utils/core_utils.h) is proven by its own case tables in tests/static; their rows are not on this page. Branch coverage of the C++ adapters is not measured.
