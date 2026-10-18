<!-- Generated from tests/unittests/traits_cases.py by `python tests/unittests/traits_cases.py`, on Python 3.12;
     tests/unittests/test_traits_cases_page.py fails when it is not current. Edit the models and cases, not this page. -->

# pygim.traits and Python's naming rules: the test cases

Two kinds of case. **Generated** cases are the coverage: each function's call space is described as data — domains of argument values, some chosen by hand (*manual*), most expanded from declared factors and bounds (*generated*) — and expanded into every call; a checker rebuilds the space from the declaration and fails a run that misses, duplicates or adds a call, or whose domain shrank. **Authored** rows, further down, are hand-picked seed and edge cases kept as regression rows and worked examples.

Every expected result comes from Python, never from pygim: `enum._is_dunder`; Python's compiler — a name compiled inside `class <C>:`, a template's qualified name, a trait's members written into the target's own source — or, where the compiler cannot be asked, the written contract. The `by` column says which.

## The models

| model | calls | called by |
| --- | --- | --- |
| `pygim::python_names::is_dunder` | 63 | every build (static_assert); GCC 16 probe |
| `pygim::python_names::owner_of` | 7 | every build (static_assert); GCC 16 probe |
| `pygim::python_names::is_private_name` | 63 | every build (static_assert); GCC 16 probe |
| `pygim::python_names::mangled` | 441 | every build (static_assert); GCC 16 probe |
| `pygim::python_names::unmangled` | 441 | every build (static_assert); GCC 16 probe |
| `pygim::python_names::respelt` | 3087 | every build (static_assert); GCC 16 probe |
| `pygim::python_names::reowned` | 3087 | every build (static_assert); GCC 16 probe |
| `pygim::python_names::requalified` | 1617 | every build (static_assert); GCC 16 probe |
| `pygim.traits.CodeBuilder: one step` | 2886 | pytest |
| `pygim.traits.CodeBuilder: two steps` | 7500 | pytest |
| `pygim.traits.CodeBuilder: refusals` | 12 | pytest |
| `pygim.traits.compose: one trait` | 208 | pytest |
| `pygim.traits.compose: two traits` | 588 | pytest |
| `pygim.utils.is_dunder` | 66 | pytest |

8806 calls of utils/python_names.h and 11260 of the Python API.

## utils/python_names.h

Every function the namespace declares, found by GCC 16 reflection (tests/static/run_python_names_callspace.py); a function or parameter without a model fails to compile there. Each parameter's domain is chosen by its role — its name in the declaration — as every parameter is a `std::string_view`.

### pygim::python_names::is_dunder

`is_dunder(name)` — called by every build and the GCC 16 probe.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `name` | names | generated from shaped names = leading (4, manual) × stem (3, manual) × trailing (4, manual) and names spelt for an owner = owner (2, manual) × body (12, generated) | 63 |

Product 63, excluded 0; required 63. Expected by: enum 63.

Baseline: `name` = `'__x__'`.

#### Vary `name`

| name | expected | by |
| --- | --- | --- |
| `''` | `False` | enum |
| `'_'` | `False` | enum |
| `'__'` | `False` | enum |
| `'___'` | `False` | enum |
| `'x'` | `False` | enum |
| `'x_'` | `False` | enum |
| `'x__'` | `False` | enum |
| `'x___'` | `False` | enum |
| `'pkg.mod'` | `False` | enum |
| `'pkg.mod_'` | `False` | enum |
| `'pkg.mod__'` | `False` | enum |
| `'pkg.mod___'` | `False` | enum |
| `'____'` | `False` | enum |
| `'_x'` | `False` | enum |
| `'_x_'` | `False` | enum |
| `'_x__'` | `False` | enum |
| `'_x___'` | `False` | enum |
| `'_pkg.mod'` | `False` | enum |
| `'_pkg.mod_'` | `False` | enum |
| `'_pkg.mod__'` | `False` | enum |
| `'_pkg.mod___'` | `False` | enum |
| `'_____'` | `False` | enum |
| `'__x'` | `False` | enum |
| `'__x_'` | `False` | enum |
| `'__x__'` | `True` | enum |
| `'__x___'` | `False` | enum |
| `'__pkg.mod'` | `False` | enum |
| `'__pkg.mod_'` | `False` | enum |
| `'__pkg.mod__'` | `True` | enum |
| `'__pkg.mod___'` | `False` | enum |
| `'______'` | `False` | enum |
| `'___x'` | `False` | enum |
| `'___x_'` | `False` | enum |
| `'___x__'` | `False` | enum |
| `'___x___'` | `False` | enum |
| `'___pkg.mod'` | `False` | enum |
| `'___pkg.mod_'` | `False` | enum |
| `'___pkg.mod__'` | `False` | enum |
| `'___pkg.mod___'` | `False` | enum |
| `'_Trait__'` | `False` | enum |
| `'_Trait___'` | `False` | enum |
| `'_Trait____'` | `False` | enum |
| `'_Trait_____'` | `False` | enum |
| `'_Trait__x'` | `False` | enum |
| `'_Trait__x_'` | `False` | enum |
| `'_Trait__x__'` | `False` | enum |
| `'_Trait__x___'` | `False` | enum |
| `'_Trait__pkg.mod'` | `False` | enum |
| `'_Trait__pkg.mod_'` | `False` | enum |
| `'_Trait__pkg.mod__'` | `False` | enum |
| `'_Trait__pkg.mod___'` | `False` | enum |
| `'_Target__'` | `False` | enum |
| `'_Target___'` | `False` | enum |
| `'_Target____'` | `False` | enum |
| `'_Target_____'` | `False` | enum |
| `'_Target__x'` | `False` | enum |
| `'_Target__x_'` | `False` | enum |
| `'_Target__x__'` | `False` | enum |
| `'_Target__x___'` | `False` | enum |
| `'_Target__pkg.mod'` | `False` | enum |
| `'_Target__pkg.mod_'` | `False` | enum |
| `'_Target__pkg.mod__'` | `False` | enum |
| `'_Target__pkg.mod___'` | `False` | enum |

### pygim::python_names::owner_of

`owner_of(class_name)` — called by every build and the GCC 16 probe.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `class_name` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |

Product 7, excluded 0; required 7. Expected by: compiler 6, contract 1.

Baseline: `class_name` = `'_Trait'`.

#### Vary `class_name`

| class_name | expected | by |
| --- | --- | --- |
| `'Trait'` | `'Trait'` | compiler |
| `'Target'` | `'Target'` | compiler |
| `'_Trait'` | `'Trait'` | compiler |
| `'__Trait'` | `'Trait'` | compiler |
| `'Trait__'` | `'Trait__'` | compiler |
| `'__'` | `''` | compiler |
| `''` | `''` | contract |

### pygim::python_names::is_private_name

`is_private_name(name)` — called by every build and the GCC 16 probe.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `name` | names | generated from shaped names = leading (4, manual) × stem (3, manual) × trailing (4, manual) and names spelt for an owner = owner (2, manual) × body (12, generated) | 63 |

Product 63, excluded 0; required 63. Expected by: compiler 62, contract 1.

Baseline: `name` = `'__x'`.

#### Vary `name`

| name | expected | by |
| --- | --- | --- |
| `''` | `False` | contract |
| `'_'` | `False` | compiler |
| `'__'` | `False` | compiler |
| `'___'` | `False` | compiler |
| `'x'` | `False` | compiler |
| `'x_'` | `False` | compiler |
| `'x__'` | `False` | compiler |
| `'x___'` | `False` | compiler |
| `'pkg.mod'` | `False` | compiler |
| `'pkg.mod_'` | `False` | compiler |
| `'pkg.mod__'` | `False` | compiler |
| `'pkg.mod___'` | `False` | compiler |
| `'____'` | `False` | compiler |
| `'_x'` | `False` | compiler |
| `'_x_'` | `False` | compiler |
| `'_x__'` | `False` | compiler |
| `'_x___'` | `False` | compiler |
| `'_pkg.mod'` | `False` | compiler |
| `'_pkg.mod_'` | `False` | compiler |
| `'_pkg.mod__'` | `False` | compiler |
| `'_pkg.mod___'` | `False` | compiler |
| `'_____'` | `False` | compiler |
| `'__x'` | `True` | compiler |
| `'__x_'` | `True` | compiler |
| `'__x__'` | `False` | compiler |
| `'__x___'` | `False` | compiler |
| `'__pkg.mod'` | `False` | compiler |
| `'__pkg.mod_'` | `False` | compiler |
| `'__pkg.mod__'` | `False` | compiler |
| `'__pkg.mod___'` | `False` | compiler |
| `'______'` | `False` | compiler |
| `'___x'` | `True` | compiler |
| `'___x_'` | `True` | compiler |
| `'___x__'` | `False` | compiler |
| `'___x___'` | `False` | compiler |
| `'___pkg.mod'` | `False` | compiler |
| `'___pkg.mod_'` | `False` | compiler |
| `'___pkg.mod__'` | `False` | compiler |
| `'___pkg.mod___'` | `False` | compiler |
| `'_Trait__'` | `False` | compiler |
| `'_Trait___'` | `False` | compiler |
| `'_Trait____'` | `False` | compiler |
| `'_Trait_____'` | `False` | compiler |
| `'_Trait__x'` | `False` | compiler |
| `'_Trait__x_'` | `False` | compiler |
| `'_Trait__x__'` | `False` | compiler |
| `'_Trait__x___'` | `False` | compiler |
| `'_Trait__pkg.mod'` | `False` | compiler |
| `'_Trait__pkg.mod_'` | `False` | compiler |
| `'_Trait__pkg.mod__'` | `False` | compiler |
| `'_Trait__pkg.mod___'` | `False` | compiler |
| `'_Target__'` | `False` | compiler |
| `'_Target___'` | `False` | compiler |
| `'_Target____'` | `False` | compiler |
| `'_Target_____'` | `False` | compiler |
| `'_Target__x'` | `False` | compiler |
| `'_Target__x_'` | `False` | compiler |
| `'_Target__x__'` | `False` | compiler |
| `'_Target__x___'` | `False` | compiler |
| `'_Target__pkg.mod'` | `False` | compiler |
| `'_Target__pkg.mod_'` | `False` | compiler |
| `'_Target__pkg.mod__'` | `False` | compiler |
| `'_Target__pkg.mod___'` | `False` | compiler |

### pygim::python_names::mangled

`mangled(class_name, name)` — called by every build and the GCC 16 probe.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `class_name` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |
| `name` | names | generated from shaped names = leading (4, manual) × stem (3, manual) × trailing (4, manual) and names spelt for an owner = owner (2, manual) × body (12, generated) | 63 |

Product 441, excluded 0; required 441. Expected by: compiler 372, compiler+contract 68, contract 1.

Baseline: `class_name` = `'Trait'`; `name` = `'__x'`.

#### Vary `class_name`

| class_name | expected | by |
| --- | --- | --- |
| `'Trait'` | `'_Trait__x'` | compiler |
| `'Target'` | `'_Target__x'` | compiler |
| `'_Trait'` | `'_Trait__x'` | compiler |
| `'__Trait'` | `'_Trait__x'` | compiler |
| `'Trait__'` | `'_Trait____x'` | compiler |
| `'__'` | `'__x'` | compiler |
| `''` | `'__x'` | compiler+contract |

#### Vary `name`

| name | expected | by |
| --- | --- | --- |
| `''` | `''` | compiler+contract |
| `'_'` | `'_'` | compiler |
| `'__'` | `'__'` | compiler |
| `'___'` | `'___'` | compiler |
| `'x'` | `'x'` | compiler |
| `'x_'` | `'x_'` | compiler |
| `'x__'` | `'x__'` | compiler |
| `'x___'` | `'x___'` | compiler |
| `'pkg.mod'` | `'pkg.mod'` | compiler |
| `'pkg.mod_'` | `'pkg.mod_'` | compiler |
| `'pkg.mod__'` | `'pkg.mod__'` | compiler |
| `'pkg.mod___'` | `'pkg.mod___'` | compiler |
| `'____'` | `'____'` | compiler |
| `'_x'` | `'_x'` | compiler |
| `'_x_'` | `'_x_'` | compiler |
| `'_x__'` | `'_x__'` | compiler |
| `'_x___'` | `'_x___'` | compiler |
| `'_pkg.mod'` | `'_pkg.mod'` | compiler |
| `'_pkg.mod_'` | `'_pkg.mod_'` | compiler |
| `'_pkg.mod__'` | `'_pkg.mod__'` | compiler |
| `'_pkg.mod___'` | `'_pkg.mod___'` | compiler |
| `'_____'` | `'_____'` | compiler |
| `'__x'` | `'_Trait__x'` | compiler |
| `'__x_'` | `'_Trait__x_'` | compiler |
| `'__x__'` | `'__x__'` | compiler |
| `'__x___'` | `'__x___'` | compiler |
| `'__pkg.mod'` | `'__pkg.mod'` | compiler |
| `'__pkg.mod_'` | `'__pkg.mod_'` | compiler |
| `'__pkg.mod__'` | `'__pkg.mod__'` | compiler |
| `'__pkg.mod___'` | `'__pkg.mod___'` | compiler |
| `'______'` | `'______'` | compiler |
| `'___x'` | `'_Trait___x'` | compiler |
| `'___x_'` | `'_Trait___x_'` | compiler |
| `'___x__'` | `'___x__'` | compiler |
| `'___x___'` | `'___x___'` | compiler |
| `'___pkg.mod'` | `'___pkg.mod'` | compiler |
| `'___pkg.mod_'` | `'___pkg.mod_'` | compiler |
| `'___pkg.mod__'` | `'___pkg.mod__'` | compiler |
| `'___pkg.mod___'` | `'___pkg.mod___'` | compiler |
| `'_Trait__'` | `'_Trait__'` | compiler |
| `'_Trait___'` | `'_Trait___'` | compiler |
| `'_Trait____'` | `'_Trait____'` | compiler |
| `'_Trait_____'` | `'_Trait_____'` | compiler |
| `'_Trait__x'` | `'_Trait__x'` | compiler |
| `'_Trait__x_'` | `'_Trait__x_'` | compiler |
| `'_Trait__x__'` | `'_Trait__x__'` | compiler |
| `'_Trait__x___'` | `'_Trait__x___'` | compiler |
| `'_Trait__pkg.mod'` | `'_Trait__pkg.mod'` | compiler |
| `'_Trait__pkg.mod_'` | `'_Trait__pkg.mod_'` | compiler |
| `'_Trait__pkg.mod__'` | `'_Trait__pkg.mod__'` | compiler |
| `'_Trait__pkg.mod___'` | `'_Trait__pkg.mod___'` | compiler |
| `'_Target__'` | `'_Target__'` | compiler |
| `'_Target___'` | `'_Target___'` | compiler |
| `'_Target____'` | `'_Target____'` | compiler |
| `'_Target_____'` | `'_Target_____'` | compiler |
| `'_Target__x'` | `'_Target__x'` | compiler |
| `'_Target__x_'` | `'_Target__x_'` | compiler |
| `'_Target__x__'` | `'_Target__x__'` | compiler |
| `'_Target__x___'` | `'_Target__x___'` | compiler |
| `'_Target__pkg.mod'` | `'_Target__pkg.mod'` | compiler |
| `'_Target__pkg.mod_'` | `'_Target__pkg.mod_'` | compiler |
| `'_Target__pkg.mod__'` | `'_Target__pkg.mod__'` | compiler |
| `'_Target__pkg.mod___'` | `'_Target__pkg.mod___'` | compiler |

### pygim::python_names::unmangled

`unmangled(class_name, name)` — called by every build and the GCC 16 probe.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `class_name` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |
| `name` | names | generated from shaped names = leading (4, manual) × stem (3, manual) × trailing (4, manual) and names spelt for an owner = owner (2, manual) × body (12, generated) | 63 |

Product 441, excluded 0; required 441. Expected by: compiler 433, compiler+contract 8.

Baseline: `class_name` = `'Trait'`; `name` = `'_Trait__x'`.

#### Vary `class_name`

| class_name | expected | by |
| --- | --- | --- |
| `'Trait'` | `'__x'` | compiler |
| `'Target'` | `''` | compiler |
| `'_Trait'` | `'__x'` | compiler |
| `'__Trait'` | `'__x'` | compiler |
| `'Trait__'` | `''` | compiler |
| `'__'` | `''` | compiler |
| `''` | `''` | compiler+contract |

#### Vary `name`

| name | expected | by |
| --- | --- | --- |
| `''` | `''` | compiler |
| `'_'` | `''` | compiler |
| `'__'` | `''` | compiler |
| `'___'` | `''` | compiler |
| `'x'` | `''` | compiler |
| `'x_'` | `''` | compiler |
| `'x__'` | `''` | compiler |
| `'x___'` | `''` | compiler |
| `'pkg.mod'` | `''` | compiler |
| `'pkg.mod_'` | `''` | compiler |
| `'pkg.mod__'` | `''` | compiler |
| `'pkg.mod___'` | `''` | compiler |
| `'____'` | `''` | compiler |
| `'_x'` | `''` | compiler |
| `'_x_'` | `''` | compiler |
| `'_x__'` | `''` | compiler |
| `'_x___'` | `''` | compiler |
| `'_pkg.mod'` | `''` | compiler |
| `'_pkg.mod_'` | `''` | compiler |
| `'_pkg.mod__'` | `''` | compiler |
| `'_pkg.mod___'` | `''` | compiler |
| `'_____'` | `''` | compiler |
| `'__x'` | `''` | compiler |
| `'__x_'` | `''` | compiler |
| `'__x__'` | `''` | compiler |
| `'__x___'` | `''` | compiler |
| `'__pkg.mod'` | `''` | compiler |
| `'__pkg.mod_'` | `''` | compiler |
| `'__pkg.mod__'` | `''` | compiler |
| `'__pkg.mod___'` | `''` | compiler |
| `'______'` | `''` | compiler |
| `'___x'` | `''` | compiler |
| `'___x_'` | `''` | compiler |
| `'___x__'` | `''` | compiler |
| `'___x___'` | `''` | compiler |
| `'___pkg.mod'` | `''` | compiler |
| `'___pkg.mod_'` | `''` | compiler |
| `'___pkg.mod__'` | `''` | compiler |
| `'___pkg.mod___'` | `''` | compiler |
| `'_Trait__'` | `''` | compiler |
| `'_Trait___'` | `''` | compiler |
| `'_Trait____'` | `''` | compiler |
| `'_Trait_____'` | `''` | compiler |
| `'_Trait__x'` | `'__x'` | compiler |
| `'_Trait__x_'` | `'__x_'` | compiler |
| `'_Trait__x__'` | `''` | compiler |
| `'_Trait__x___'` | `''` | compiler |
| `'_Trait__pkg.mod'` | `''` | compiler |
| `'_Trait__pkg.mod_'` | `''` | compiler |
| `'_Trait__pkg.mod__'` | `''` | compiler |
| `'_Trait__pkg.mod___'` | `''` | compiler |
| `'_Target__'` | `''` | compiler |
| `'_Target___'` | `''` | compiler |
| `'_Target____'` | `''` | compiler |
| `'_Target_____'` | `''` | compiler |
| `'_Target__x'` | `''` | compiler |
| `'_Target__x_'` | `''` | compiler |
| `'_Target__x__'` | `''` | compiler |
| `'_Target__x___'` | `''` | compiler |
| `'_Target__pkg.mod'` | `''` | compiler |
| `'_Target__pkg.mod_'` | `''` | compiler |
| `'_Target__pkg.mod__'` | `''` | compiler |
| `'_Target__pkg.mod___'` | `''` | compiler |

### pygim::python_names::respelt

`respelt(from, to, name)` — called by every build and the GCC 16 probe.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `from` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |
| `to` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |
| `name` | names | generated from shaped names = leading (4, manual) × stem (3, manual) × trailing (4, manual) and names spelt for an owner = owner (2, manual) × body (12, generated) | 63 |

Product 3087, excluded 0; required 3087. Expected by: compiler 3019, compiler+contract 68.

Baseline: `from` = `'Trait'`; `to` = `'Target'`; `name` = `'_Trait__x'`.

#### Vary `from`

| from | expected | by |
| --- | --- | --- |
| `'Trait'` | `'_Target__x'` | compiler |
| `'Target'` | `None` | compiler |
| `'_Trait'` | `'_Target__x'` | compiler |
| `'__Trait'` | `'_Target__x'` | compiler |
| `'Trait__'` | `None` | compiler |
| `'__'` | `None` | compiler |
| `''` | `None` | compiler+contract |

#### Vary `to`

| to | expected | by |
| --- | --- | --- |
| `'Trait'` | `'_Trait__x'` | compiler |
| `'Target'` | `'_Target__x'` | compiler |
| `'_Trait'` | `'_Trait__x'` | compiler |
| `'__Trait'` | `'_Trait__x'` | compiler |
| `'Trait__'` | `'_Trait____x'` | compiler |
| `'__'` | `'__x'` | compiler |
| `''` | `'__x'` | compiler+contract |

#### Vary `name`

| name | expected | by |
| --- | --- | --- |
| `''` | `None` | compiler |
| `'_'` | `None` | compiler |
| `'__'` | `None` | compiler |
| `'___'` | `None` | compiler |
| `'x'` | `None` | compiler |
| `'x_'` | `None` | compiler |
| `'x__'` | `None` | compiler |
| `'x___'` | `None` | compiler |
| `'pkg.mod'` | `None` | compiler |
| `'pkg.mod_'` | `None` | compiler |
| `'pkg.mod__'` | `None` | compiler |
| `'pkg.mod___'` | `None` | compiler |
| `'____'` | `None` | compiler |
| `'_x'` | `None` | compiler |
| `'_x_'` | `None` | compiler |
| `'_x__'` | `None` | compiler |
| `'_x___'` | `None` | compiler |
| `'_pkg.mod'` | `None` | compiler |
| `'_pkg.mod_'` | `None` | compiler |
| `'_pkg.mod__'` | `None` | compiler |
| `'_pkg.mod___'` | `None` | compiler |
| `'_____'` | `None` | compiler |
| `'__x'` | `None` | compiler |
| `'__x_'` | `None` | compiler |
| `'__x__'` | `None` | compiler |
| `'__x___'` | `None` | compiler |
| `'__pkg.mod'` | `None` | compiler |
| `'__pkg.mod_'` | `None` | compiler |
| `'__pkg.mod__'` | `None` | compiler |
| `'__pkg.mod___'` | `None` | compiler |
| `'______'` | `None` | compiler |
| `'___x'` | `None` | compiler |
| `'___x_'` | `None` | compiler |
| `'___x__'` | `None` | compiler |
| `'___x___'` | `None` | compiler |
| `'___pkg.mod'` | `None` | compiler |
| `'___pkg.mod_'` | `None` | compiler |
| `'___pkg.mod__'` | `None` | compiler |
| `'___pkg.mod___'` | `None` | compiler |
| `'_Trait__'` | `None` | compiler |
| `'_Trait___'` | `None` | compiler |
| `'_Trait____'` | `None` | compiler |
| `'_Trait_____'` | `None` | compiler |
| `'_Trait__x'` | `'_Target__x'` | compiler |
| `'_Trait__x_'` | `'_Target__x_'` | compiler |
| `'_Trait__x__'` | `None` | compiler |
| `'_Trait__x___'` | `None` | compiler |
| `'_Trait__pkg.mod'` | `None` | compiler |
| `'_Trait__pkg.mod_'` | `None` | compiler |
| `'_Trait__pkg.mod__'` | `None` | compiler |
| `'_Trait__pkg.mod___'` | `None` | compiler |
| `'_Target__'` | `None` | compiler |
| `'_Target___'` | `None` | compiler |
| `'_Target____'` | `None` | compiler |
| `'_Target_____'` | `None` | compiler |
| `'_Target__x'` | `None` | compiler |
| `'_Target__x_'` | `None` | compiler |
| `'_Target__x__'` | `None` | compiler |
| `'_Target__x___'` | `None` | compiler |
| `'_Target__pkg.mod'` | `None` | compiler |
| `'_Target__pkg.mod_'` | `None` | compiler |
| `'_Target__pkg.mod__'` | `None` | compiler |
| `'_Target__pkg.mod___'` | `None` | compiler |

### pygim::python_names::reowned

`reowned(from, to, name)` — called by every build and the GCC 16 probe.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `from` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |
| `to` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |
| `name` | names | generated from shaped names = leading (4, manual) × stem (3, manual) × trailing (4, manual) and names spelt for an owner = owner (2, manual) × body (12, generated) | 63 |

Product 3087, excluded 0; required 3087. Expected by: compiler 3019, compiler+contract 68.

Baseline: `from` = `'Trait'`; `to` = `'Target'`; `name` = `'_Trait__x'`.

#### Vary `from`

| from | expected | by |
| --- | --- | --- |
| `'Trait'` | `'_Target__x'` | compiler |
| `'Target'` | `'_Trait__x'` | compiler |
| `'_Trait'` | `'_Target__x'` | compiler |
| `'__Trait'` | `'_Target__x'` | compiler |
| `'Trait__'` | `'_Trait__x'` | compiler |
| `'__'` | `'_Trait__x'` | compiler |
| `''` | `'_Trait__x'` | compiler+contract |

#### Vary `to`

| to | expected | by |
| --- | --- | --- |
| `'Trait'` | `'_Trait__x'` | compiler |
| `'Target'` | `'_Target__x'` | compiler |
| `'_Trait'` | `'_Trait__x'` | compiler |
| `'__Trait'` | `'_Trait__x'` | compiler |
| `'Trait__'` | `'_Trait____x'` | compiler |
| `'__'` | `'__x'` | compiler |
| `''` | `'__x'` | compiler+contract |

#### Vary `name`

| name | expected | by |
| --- | --- | --- |
| `''` | `''` | compiler |
| `'_'` | `'_'` | compiler |
| `'__'` | `'__'` | compiler |
| `'___'` | `'___'` | compiler |
| `'x'` | `'x'` | compiler |
| `'x_'` | `'x_'` | compiler |
| `'x__'` | `'x__'` | compiler |
| `'x___'` | `'x___'` | compiler |
| `'pkg.mod'` | `'pkg.mod'` | compiler |
| `'pkg.mod_'` | `'pkg.mod_'` | compiler |
| `'pkg.mod__'` | `'pkg.mod__'` | compiler |
| `'pkg.mod___'` | `'pkg.mod___'` | compiler |
| `'____'` | `'____'` | compiler |
| `'_x'` | `'_x'` | compiler |
| `'_x_'` | `'_x_'` | compiler |
| `'_x__'` | `'_x__'` | compiler |
| `'_x___'` | `'_x___'` | compiler |
| `'_pkg.mod'` | `'_pkg.mod'` | compiler |
| `'_pkg.mod_'` | `'_pkg.mod_'` | compiler |
| `'_pkg.mod__'` | `'_pkg.mod__'` | compiler |
| `'_pkg.mod___'` | `'_pkg.mod___'` | compiler |
| `'_____'` | `'_____'` | compiler |
| `'__x'` | `'__x'` | compiler |
| `'__x_'` | `'__x_'` | compiler |
| `'__x__'` | `'__x__'` | compiler |
| `'__x___'` | `'__x___'` | compiler |
| `'__pkg.mod'` | `'__pkg.mod'` | compiler |
| `'__pkg.mod_'` | `'__pkg.mod_'` | compiler |
| `'__pkg.mod__'` | `'__pkg.mod__'` | compiler |
| `'__pkg.mod___'` | `'__pkg.mod___'` | compiler |
| `'______'` | `'______'` | compiler |
| `'___x'` | `'___x'` | compiler |
| `'___x_'` | `'___x_'` | compiler |
| `'___x__'` | `'___x__'` | compiler |
| `'___x___'` | `'___x___'` | compiler |
| `'___pkg.mod'` | `'___pkg.mod'` | compiler |
| `'___pkg.mod_'` | `'___pkg.mod_'` | compiler |
| `'___pkg.mod__'` | `'___pkg.mod__'` | compiler |
| `'___pkg.mod___'` | `'___pkg.mod___'` | compiler |
| `'_Trait__'` | `'_Trait__'` | compiler |
| `'_Trait___'` | `'_Trait___'` | compiler |
| `'_Trait____'` | `'_Trait____'` | compiler |
| `'_Trait_____'` | `'_Trait_____'` | compiler |
| `'_Trait__x'` | `'_Target__x'` | compiler |
| `'_Trait__x_'` | `'_Target__x_'` | compiler |
| `'_Trait__x__'` | `'_Trait__x__'` | compiler |
| `'_Trait__x___'` | `'_Trait__x___'` | compiler |
| `'_Trait__pkg.mod'` | `'_Trait__pkg.mod'` | compiler |
| `'_Trait__pkg.mod_'` | `'_Trait__pkg.mod_'` | compiler |
| `'_Trait__pkg.mod__'` | `'_Trait__pkg.mod__'` | compiler |
| `'_Trait__pkg.mod___'` | `'_Trait__pkg.mod___'` | compiler |
| `'_Target__'` | `'_Target__'` | compiler |
| `'_Target___'` | `'_Target___'` | compiler |
| `'_Target____'` | `'_Target____'` | compiler |
| `'_Target_____'` | `'_Target_____'` | compiler |
| `'_Target__x'` | `'_Target__x'` | compiler |
| `'_Target__x_'` | `'_Target__x_'` | compiler |
| `'_Target__x__'` | `'_Target__x__'` | compiler |
| `'_Target__x___'` | `'_Target__x___'` | compiler |
| `'_Target__pkg.mod'` | `'_Target__pkg.mod'` | compiler |
| `'_Target__pkg.mod_'` | `'_Target__pkg.mod_'` | compiler |
| `'_Target__pkg.mod__'` | `'_Target__pkg.mod__'` | compiler |
| `'_Target__pkg.mod___'` | `'_Target__pkg.mod___'` | compiler |

### pygim::python_names::requalified

`requalified(from, to, qualname)` — called by every build and the GCC 16 probe.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `from` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |
| `to` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |
| `qualname` | qualified names | generated from owned qualified names = owner (7, manual) × tail (4, manual) and unowned qualified names (5, manual) | 33 |

Product 1617, excluded 0; required 1617. Expected by: compiler 144, contract 1473.

Baseline: `from` = `'Trait'`; `to` = `'Target'`; `qualname` = `'Trait.f'`.

#### Vary `from`

| from | expected | by |
| --- | --- | --- |
| `'Trait'` | `'Target.f'` | compiler |
| `'Target'` | `'Trait.f'` | contract |
| `'_Trait'` | `'Trait.f'` | contract |
| `'__Trait'` | `'Trait.f'` | contract |
| `'Trait__'` | `'Trait.f'` | contract |
| `'__'` | `'Trait.f'` | contract |
| `''` | `'Trait.f'` | contract |

#### Vary `to`

| to | expected | by |
| --- | --- | --- |
| `'Trait'` | `'Trait.f'` | compiler |
| `'Target'` | `'Target.f'` | compiler |
| `'_Trait'` | `'_Trait.f'` | compiler |
| `'__Trait'` | `'__Trait.f'` | compiler |
| `'Trait__'` | `'Trait__.f'` | compiler |
| `'__'` | `'__.f'` | compiler |
| `''` | `'.f'` | contract |

#### Vary `qualname`

| qualname | expected | by |
| --- | --- | --- |
| `'Trait.f'` | `'Target.f'` | compiler |
| `'Trait.f.<locals>.<lambda>'` | `'Target.f.<locals>.<lambda>'` | compiler |
| `'Trait.f.<locals>.g'` | `'Target.f.<locals>.g'` | compiler |
| `'Trait.Inner.f'` | `'Target.Inner.f'` | compiler |
| `'Target.f'` | `'Target.f'` | contract |
| `'Target.f.<locals>.<lambda>'` | `'Target.f.<locals>.<lambda>'` | contract |
| `'Target.f.<locals>.g'` | `'Target.f.<locals>.g'` | contract |
| `'Target.Inner.f'` | `'Target.Inner.f'` | contract |
| `'_Trait.f'` | `'_Trait.f'` | contract |
| `'_Trait.f.<locals>.<lambda>'` | `'_Trait.f.<locals>.<lambda>'` | contract |
| `'_Trait.f.<locals>.g'` | `'_Trait.f.<locals>.g'` | contract |
| `'_Trait.Inner.f'` | `'_Trait.Inner.f'` | contract |
| `'__Trait.f'` | `'__Trait.f'` | contract |
| `'__Trait.f.<locals>.<lambda>'` | `'__Trait.f.<locals>.<lambda>'` | contract |
| `'__Trait.f.<locals>.g'` | `'__Trait.f.<locals>.g'` | contract |
| `'__Trait.Inner.f'` | `'__Trait.Inner.f'` | contract |
| `'Trait__.f'` | `'Trait__.f'` | contract |
| `'Trait__.f.<locals>.<lambda>'` | `'Trait__.f.<locals>.<lambda>'` | contract |
| `'Trait__.f.<locals>.g'` | `'Trait__.f.<locals>.g'` | contract |
| `'Trait__.Inner.f'` | `'Trait__.Inner.f'` | contract |
| `'__.f'` | `'__.f'` | contract |
| `'__.f.<locals>.<lambda>'` | `'__.f.<locals>.<lambda>'` | contract |
| `'__.f.<locals>.g'` | `'__.f.<locals>.g'` | contract |
| `'__.Inner.f'` | `'__.Inner.f'` | contract |
| `'.f'` | `'.f'` | contract |
| `'.f.<locals>.<lambda>'` | `'.f.<locals>.<lambda>'` | contract |
| `'.f.<locals>.g'` | `'.f.<locals>.g'` | contract |
| `'.Inner.f'` | `'.Inner.f'` | contract |
| `'f'` | `'f'` | contract |
| `'TraitX.f'` | `'TraitX.f'` | contract |
| `'Trait'` | `'Trait'` | contract |
| `'Trait.'` | `'Target.'` | contract |
| `''` | `''` | contract |

## The Python API

Classes and code objects are made by factories from the data in each cell: a code template compiled inside the owner class, a class from its qualified name, a target from its source. Targets and traits are made fresh for every case. Besides the cells shown, every CodeBuilder case expects the rest of each code object unchanged, the code passed in unchanged and two builds equal; every compose() case that writes expects the traits untouched and the record's `defined` and `composed` locations.

### pygim.traits.CodeBuilder: one step

`CodeBuilder(template(name) compiled in class owner).reown(*step).build()` — called by pytest.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `template` | code templates | manual: each place a method writes a name: an attribute, a global, inside a lambda and inside a nested function, an import, a local variable | 6 |
| `name` | names in the code | manual: one of each kind the name rules tell apart — private, dunder, internal, public, spelt for the owner, spelt for a target, dotted; the rules themselves are proven over the whole name domain | 7 |
| `owner` | owners of the code | manual: the class the code is compiled in: a plain name, a leading underscore mangling drops, only underscores (which mangle nothing) | 3 |
| `step` | one step or none | generated from no step (1, manual) and steps = source (5, manual) × target (5, manual) | 26 |

Product 3276, excluded 390 — a dotted name is written only by an import (390); required 2886. Expected by: compiler 2886.

Baseline: `template` = `'attribute'`; `name` = `'__x'`; `owner` = `'Trait'`; `step` = `('Trait', 'Target')`.

#### Vary `template`

| template | expected | by |
| --- | --- | --- |
| `'attribute'` | `('_Target__x',) Target.f` | compiler |
| `'global'` | `('_Target__x',) Target.f` | compiler |
| `'lambda'` | `() Target.f` · `('_Target__x',) Target.f.<locals>.<lambda>` | compiler |
| `'nested def'` | `() Target.f` · `('_Target__x',) Target.f.<locals>.g` | compiler |
| `'import'` | `('_Target__x',) Target.f` ⚠ divergence | compiler |
| `'local'` | `() Target.f` ⚠ divergence | compiler |

#### Vary `name`

| name | expected | by |
| --- | --- | --- |
| `'__x'` | `('_Target__x',) Target.f` | compiler |
| `'__x__'` | `('__x__',) Target.f` | compiler |
| `'_x'` | `('_x',) Target.f` | compiler |
| `'x'` | `('x',) Target.f` | compiler |
| `'_Trait__x'` | `('_Target__x',) Target.f` | compiler |
| `'_Target__x'` | `('_Target__x',) Target.f` | compiler |

#### Vary `owner`

| owner | expected | by |
| --- | --- | --- |
| `'Trait'` | `('_Target__x',) Target.f` | compiler |
| `'_Trait'` | `('_Target__x',) _Trait.f` | compiler |
| `'__'` | `('__x',) __.f` | compiler |

#### Vary `step`

| step | expected | by |
| --- | --- | --- |
| `None` | `('_Trait__x',) Trait.f` | compiler |
| `('Trait', 'Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('Trait', '_Trait')` | `('_Trait__x',) _Trait.f` | compiler |
| `('Trait', '__')` | `('__x',) __.f` | compiler |
| `('Trait', 'Target')` | `('_Target__x',) Target.f` | compiler |
| `('Trait', 'Outer.Target')` | `('_Target__x',) Outer.Target.f` | compiler |
| `('_Trait', 'Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('_Trait', '_Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('_Trait', '__')` | `('__x',) Trait.f` | compiler |
| `('_Trait', 'Target')` | `('_Target__x',) Trait.f` | compiler |
| `('_Trait', 'Outer.Target')` | `('_Target__x',) Trait.f` | compiler |
| `('__', 'Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('__', '_Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('__', '__')` | `('_Trait__x',) Trait.f` | compiler |
| `('__', 'Target')` | `('_Trait__x',) Trait.f` | compiler |
| `('__', 'Outer.Target')` | `('_Trait__x',) Trait.f` | compiler |
| `('Target', 'Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('Target', '_Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('Target', '__')` | `('_Trait__x',) Trait.f` | compiler |
| `('Target', 'Target')` | `('_Trait__x',) Trait.f` | compiler |
| `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` | compiler |
| `('Outer.Target', 'Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('Outer.Target', '_Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('Outer.Target', '__')` | `('_Trait__x',) Trait.f` | compiler |
| `('Outer.Target', 'Target')` | `('_Trait__x',) Trait.f` | compiler |
| `('Outer.Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` | compiler |

### pygim.traits.CodeBuilder: two steps

`CodeBuilder(attribute(name) compiled in class owner).reown(*steps[0]).reown(*steps[1]).build()` — called by pytest.

A reduction: every chain of two steps, crossed with the attribute template only and with the names a step can move. build() applies the same move to every name of every code object in the tree, which the one-step model shows over every template and kind of name.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `name` | names a chain can move | manual: the names a step can respell, and a public one it never does | 4 |
| `owner` | owners of the code | manual: the class the code is compiled in: a plain name, a leading underscore mangling drops, only underscores (which mangle nothing) | 3 |
| `steps[0]` | steps | generated: steps = source (5, manual) × target (5, manual) | 25 |
| `steps[1]` | steps | generated: steps = source (5, manual) × target (5, manual) | 25 |

Product 7500, excluded 0; required 7500. Expected by: compiler 7500.

Baseline: `name` = `'__x'`; `owner` = `'Trait'`; `steps[0]` = `('Trait', 'Target')`; `steps[1]` = `('Target', 'Outer.Target')`.

#### Vary `name`

| name | expected | by |
| --- | --- | --- |
| `'__x'` | `('_Target__x',) Outer.Target.f` | compiler |
| `'_Trait__x'` | `('_Target__x',) Outer.Target.f` | compiler |
| `'_Target__x'` | `('_Target__x',) Outer.Target.f` | compiler |
| `'x'` | `('x',) Outer.Target.f` | compiler |

#### Vary `owner`

| owner | expected | by |
| --- | --- | --- |
| `'Trait'` | `('_Target__x',) Outer.Target.f` | compiler |
| `'_Trait'` | `('_Target__x',) _Trait.f` | compiler |
| `'__'` | `('__x',) __.f` | compiler |

#### Vary `steps[0]`

| steps[0] | expected | by |
| --- | --- | --- |
| `('Trait', 'Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('Trait', '_Trait')` | `('_Trait__x',) _Trait.f` | compiler |
| `('Trait', '__')` | `('__x',) __.f` | compiler |
| `('Trait', 'Target')` | `('_Target__x',) Outer.Target.f` | compiler |
| `('Trait', 'Outer.Target')` | `('_Target__x',) Outer.Target.f` | compiler |
| `('_Trait', 'Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('_Trait', '_Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('_Trait', '__')` | `('__x',) Trait.f` | compiler |
| `('_Trait', 'Target')` | `('_Target__x',) Trait.f` | compiler |
| `('_Trait', 'Outer.Target')` | `('_Target__x',) Trait.f` | compiler |
| `('__', 'Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('__', '_Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('__', '__')` | `('_Trait__x',) Trait.f` | compiler |
| `('__', 'Target')` | `('_Trait__x',) Trait.f` | compiler |
| `('__', 'Outer.Target')` | `('_Trait__x',) Trait.f` | compiler |
| `('Target', 'Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('Target', '_Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('Target', '__')` | `('_Trait__x',) Trait.f` | compiler |
| `('Target', 'Target')` | `('_Trait__x',) Trait.f` | compiler |
| `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` | compiler |
| `('Outer.Target', 'Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('Outer.Target', '_Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('Outer.Target', '__')` | `('_Trait__x',) Trait.f` | compiler |
| `('Outer.Target', 'Target')` | `('_Trait__x',) Trait.f` | compiler |
| `('Outer.Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` | compiler |

#### Vary `steps[1]`

| steps[1] | expected | by |
| --- | --- | --- |
| `('Trait', 'Trait')` | `('_Target__x',) Target.f` | compiler |
| `('Trait', '_Trait')` | `('_Target__x',) Target.f` | compiler |
| `('Trait', '__')` | `('_Target__x',) Target.f` | compiler |
| `('Trait', 'Target')` | `('_Target__x',) Target.f` | compiler |
| `('Trait', 'Outer.Target')` | `('_Target__x',) Target.f` | compiler |
| `('_Trait', 'Trait')` | `('_Target__x',) Target.f` | compiler |
| `('_Trait', '_Trait')` | `('_Target__x',) Target.f` | compiler |
| `('_Trait', '__')` | `('_Target__x',) Target.f` | compiler |
| `('_Trait', 'Target')` | `('_Target__x',) Target.f` | compiler |
| `('_Trait', 'Outer.Target')` | `('_Target__x',) Target.f` | compiler |
| `('__', 'Trait')` | `('_Target__x',) Target.f` | compiler |
| `('__', '_Trait')` | `('_Target__x',) Target.f` | compiler |
| `('__', '__')` | `('_Target__x',) Target.f` | compiler |
| `('__', 'Target')` | `('_Target__x',) Target.f` | compiler |
| `('__', 'Outer.Target')` | `('_Target__x',) Target.f` | compiler |
| `('Target', 'Trait')` | `('_Trait__x',) Trait.f` | compiler |
| `('Target', '_Trait')` | `('_Trait__x',) _Trait.f` | compiler |
| `('Target', '__')` | `('__x',) __.f` | compiler |
| `('Target', 'Target')` | `('_Target__x',) Target.f` | compiler |
| `('Target', 'Outer.Target')` | `('_Target__x',) Outer.Target.f` | compiler |
| `('Outer.Target', 'Trait')` | `('_Trait__x',) Target.f` | compiler |
| `('Outer.Target', '_Trait')` | `('_Trait__x',) Target.f` | compiler |
| `('Outer.Target', '__')` | `('__x',) Target.f` | compiler |
| `('Outer.Target', 'Target')` | `('_Target__x',) Target.f` | compiler |
| `('Outer.Target', 'Outer.Target')` | `('_Target__x',) Target.f` | compiler |

### pygim.traits.CodeBuilder: refusals

`CodeBuilder(code).reown(source, target), one argument wrong` — called by pytest.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `position` | the argument that is wrong | manual: each argument CodeBuilder takes, wrong alone | 3 |
| `wrong` | wrong arguments | manual: source text instead of code, the function instead of its code, None, an instance instead of its class | 4 |

Product 12, excluded 0; required 12. Expected by: contract 12.

Baseline: `position` = `'code'`; `wrong` = `'text'`.

#### Vary `position`

| position | expected | by |
| --- | --- | --- |
| `'code'` | `raises TypeError: CodeBuilder takes a code object, not str` | contract |
| `'source'` | `raises TypeError` | contract |
| `'target'` | `raises TypeError` | contract |

#### Vary `wrong`

| wrong | expected | by |
| --- | --- | --- |
| `'text'` | `raises TypeError: CodeBuilder takes a code object, not str` | contract |
| `'function'` | `raises TypeError: CodeBuilder takes a code object, not function` | contract |
| `'none'` | `raises TypeError: CodeBuilder takes a code object, not NoneType` | contract |
| `'instance'` | `raises TypeError: CodeBuilder takes a code object, not Target` | contract |

### pygim.traits.compose: one trait

`compose(target, *traits[:1], override=override); traits_of(target)` — called by pytest.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `target` | targets | manual: a plain class; one with a base; one that defines a name a trait has; a frozen slots dataclass (rebuilt by its decorator); an Enum (a metaclass of its own); an instance instead of a class | 6 |
| `traits[0]` | one trait or none | generated from none (1, manual) and traits (17, generated) | 18 |
| `override` | override | manual: both | 2 |

Product 216, excluded 8 — Python itself cannot answer: zero-argument super() written in a slots dataclass raises TypeError (CPython gh-90562), so the member written there is no oracle (8); required 208. Expected by: contract 73, contract+compiler 135.

Baseline: `target` = `'plain'`; `traits[0]` = `('function', 'private', 'greet')`; `override` = `False`.

#### Vary `target`

| target | expected | by |
| --- | --- | --- |
| `'plain'` | `writes greet; record greet ← TraitA; greet → 'TraitA:ann' (Person.greet)` | contract+compiler |
| `'with a base'` | `writes greet; record greet ← TraitA; greet → 'TraitA:ann' (Child.greet)` | contract+compiler |
| `'with greet'` | `raises ValueError: Person already has greet; traits_callspace.generated.TraitA would replace it (pass override=True)` | contract |
| `'frozen slots dataclass'` | `writes greet; record greet ← TraitA; greet → raises AttributeError (Point.greet)` | contract+compiler |
| `'enum'` | `writes greet; record greet ← TraitA; greet → raises AttributeError (Colour.greet)` | contract+compiler |
| `'not a class'` | `raises TypeError` | contract |

#### Vary `traits[0]`

| traits[0] | expected | by |
| --- | --- | --- |
| `None` | `writes nothing` | contract+compiler |
| `('function', 'plain', 'greet')` | `writes greet; record greet ← TraitA; greet → 'TraitA' (Person.greet)` | contract+compiler |
| `('function', 'plain', 'hello')` | `writes hello; record hello ← TraitA; hello → 'TraitA' (Person.hello)` | contract+compiler |
| `('function', 'private', 'greet')` | `writes greet; record greet ← TraitA; greet → 'TraitA:ann' (Person.greet)` | contract+compiler |
| `('function', 'private', 'hello')` | `writes hello; record hello ← TraitA; hello → 'TraitA:ann' (Person.hello)` | contract+compiler |
| `('function', 'super', 'greet')` | `writes greet; record greet ← TraitA; greet → raises AttributeError (Person.greet)` | contract+compiler |
| `('function', 'super', 'hello')` | `writes hello; record hello ← TraitA; hello → raises AttributeError (Person.hello)` | contract+compiler |
| `('property', 'private', 'greet')` | `writes greet; record greet ← TraitA; greet → 'TraitA:ann' (Person.greet)` | contract+compiler |
| `('property', 'private', 'hello')` | `writes hello; record hello ← TraitA; hello → 'TraitA:ann' (Person.hello)` | contract+compiler |
| `('property', 'super', 'greet')` | `writes greet; record greet ← TraitA; greet → raises AttributeError (Person.greet)` | contract+compiler |
| `('property', 'super', 'hello')` | `writes hello; record hello ← TraitA; hello → raises AttributeError (Person.hello)` | contract+compiler |
| `('classmethod', 'private', 'greet')` | `writes greet; record greet ← TraitA; greet → 'TraitA:person' (Person.greet)` | contract+compiler |
| `('classmethod', 'private', 'hello')` | `writes hello; record hello ← TraitA; hello → 'TraitA:person' (Person.hello)` | contract+compiler |
| `('staticmethod', 'plain', 'greet')` | `writes greet; record greet ← TraitA; greet → 'TraitA' (Person.greet)` | contract+compiler |
| `('staticmethod', 'plain', 'hello')` | `writes hello; record hello ← TraitA; hello → 'TraitA' (Person.hello)` | contract+compiler |
| `('constant', 'plain', 'greet')` | `raises TypeError: TraitA.greet is int; a trait composes functions, properties, classmethods and staticmethods` | contract |
| `('constant', 'plain', 'hello')` | `raises TypeError: TraitA.hello is int; a trait composes functions, properties, classmethods and staticmethods` | contract |
| `'not a class'` | `raises TypeError: compose takes classes as traits, not function` | contract |

#### Vary `override`

| override | expected | by |
| --- | --- | --- |
| `False` | `writes greet; record greet ← TraitA; greet → 'TraitA:ann' (Person.greet)` | contract+compiler |
| `True` | `writes greet; record greet ← TraitA; greet → 'TraitA:ann' (Person.greet)` | contract+compiler |

### pygim.traits.compose: two traits

`compose(target, traits[0], traits[1], override=override); traits_of(target)` — called by pytest.

A reduction: a second trait meets the first through its names (a conflict, which trait wins, what the record says) and its kind (a refusal stops the call), so two traits are crossed over a function, a property and a constant under each name; every member body is crossed with every target in the one-trait model.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `target` | targets | manual: a plain class; one with a base; one that defines a name a trait has; a frozen slots dataclass (rebuilt by its decorator); an Enum (a metaclass of its own); an instance instead of a class | 6 |
| `traits[0]` | traits, for two | generated from trait classes = member (3, manual) × name (2, manual) and not a trait (1, manual) | 7 |
| `traits[1]` | traits, for two | generated from trait classes = member (3, manual) × name (2, manual) and not a trait (1, manual) | 7 |
| `override` | override | manual: both | 2 |

Product 588, excluded 0; required 588. Expected by: contract 476, contract+compiler 112.

Baseline: `target` = `'plain'`; `traits[0]` = `('function', 'private', 'greet')`; `traits[1]` = `('function', 'private', 'hello')`; `override` = `False`.

#### Vary `target`

| target | expected | by |
| --- | --- | --- |
| `'plain'` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → 'TraitA:ann' (Person.greet); hello → 'TraitB:ann' (Person.hello)` | contract+compiler |
| `'with a base'` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → 'TraitA:ann' (Child.greet); hello → 'TraitB:ann' (Child.hello)` | contract+compiler |
| `'with greet'` | `raises ValueError: Person already has greet; traits_callspace.generated.TraitA would replace it (pass override=True)` | contract |
| `'frozen slots dataclass'` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → raises AttributeError (Point.greet); hello → raises AttributeError (Point.hello)` | contract+compiler |
| `'enum'` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → raises AttributeError (Colour.greet); hello → raises AttributeError (Colour.hello)` | contract+compiler |
| `'not a class'` | `raises TypeError` | contract |

#### Vary `traits[0]`

| traits[0] | expected | by |
| --- | --- | --- |
| `('function', 'private', 'greet')` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → 'TraitA:ann' (Person.greet); hello → 'TraitB:ann' (Person.hello)` | contract+compiler |
| `('function', 'private', 'hello')` | `raises ValueError: traits_callspace.generated.TraitA and traits_callspace.generated.TraitB both define hello (pass override=True to let the later win)` | contract |
| `('property', 'private', 'greet')` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → 'TraitA:ann' (Person.greet); hello → 'TraitB:ann' (Person.hello)` | contract+compiler |
| `('property', 'private', 'hello')` | `raises ValueError: traits_callspace.generated.TraitA and traits_callspace.generated.TraitB both define hello (pass override=True to let the later win)` | contract |
| `('constant', 'plain', 'greet')` | `raises TypeError: TraitA.greet is int; a trait composes functions, properties, classmethods and staticmethods` | contract |
| `('constant', 'plain', 'hello')` | `raises TypeError: TraitA.hello is int; a trait composes functions, properties, classmethods and staticmethods` | contract |
| `'not a class'` | `raises TypeError: compose takes classes as traits, not function` | contract |

#### Vary `traits[1]`

| traits[1] | expected | by |
| --- | --- | --- |
| `('function', 'private', 'greet')` | `raises ValueError: traits_callspace.generated.TraitA and traits_callspace.generated.TraitB both define greet (pass override=True to let the later win)` | contract |
| `('function', 'private', 'hello')` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → 'TraitA:ann' (Person.greet); hello → 'TraitB:ann' (Person.hello)` | contract+compiler |
| `('property', 'private', 'greet')` | `raises ValueError: traits_callspace.generated.TraitA and traits_callspace.generated.TraitB both define greet (pass override=True to let the later win)` | contract |
| `('property', 'private', 'hello')` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → 'TraitA:ann' (Person.greet); hello → 'TraitB:ann' (Person.hello)` | contract+compiler |
| `('constant', 'plain', 'greet')` | `raises TypeError: TraitB.greet is int; a trait composes functions, properties, classmethods and staticmethods` | contract |
| `('constant', 'plain', 'hello')` | `raises TypeError: TraitB.hello is int; a trait composes functions, properties, classmethods and staticmethods` | contract |
| `'not a class'` | `raises TypeError: compose takes classes as traits, not function` | contract |

#### Vary `override`

| override | expected | by |
| --- | --- | --- |
| `False` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → 'TraitA:ann' (Person.greet); hello → 'TraitB:ann' (Person.hello)` | contract+compiler |
| `True` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → 'TraitA:ann' (Person.greet); hello → 'TraitB:ann' (Person.hello)` | contract+compiler |

### pygim.utils.is_dunder

`is_dunder(name)` — called by pytest.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `name` | names and not names | generated from names (63, generated) and not text (3, manual) | 66 |

Product 66, excluded 0; required 66. Expected by: contract 3, enum 63.

Baseline: `name` = `'__x__'`.

#### Vary `name`

| name | expected | by |
| --- | --- | --- |
| `''` | `False` | enum |
| `'_'` | `False` | enum |
| `'__'` | `False` | enum |
| `'___'` | `False` | enum |
| `'x'` | `False` | enum |
| `'x_'` | `False` | enum |
| `'x__'` | `False` | enum |
| `'x___'` | `False` | enum |
| `'pkg.mod'` | `False` | enum |
| `'pkg.mod_'` | `False` | enum |
| `'pkg.mod__'` | `False` | enum |
| `'pkg.mod___'` | `False` | enum |
| `'____'` | `False` | enum |
| `'_x'` | `False` | enum |
| `'_x_'` | `False` | enum |
| `'_x__'` | `False` | enum |
| `'_x___'` | `False` | enum |
| `'_pkg.mod'` | `False` | enum |
| `'_pkg.mod_'` | `False` | enum |
| `'_pkg.mod__'` | `False` | enum |
| `'_pkg.mod___'` | `False` | enum |
| `'_____'` | `False` | enum |
| `'__x'` | `False` | enum |
| `'__x_'` | `False` | enum |
| `'__x__'` | `True` | enum |
| `'__x___'` | `False` | enum |
| `'__pkg.mod'` | `False` | enum |
| `'__pkg.mod_'` | `False` | enum |
| `'__pkg.mod__'` | `True` | enum |
| `'__pkg.mod___'` | `False` | enum |
| `'______'` | `False` | enum |
| `'___x'` | `False` | enum |
| `'___x_'` | `False` | enum |
| `'___x__'` | `False` | enum |
| `'___x___'` | `False` | enum |
| `'___pkg.mod'` | `False` | enum |
| `'___pkg.mod_'` | `False` | enum |
| `'___pkg.mod__'` | `False` | enum |
| `'___pkg.mod___'` | `False` | enum |
| `'_Trait__'` | `False` | enum |
| `'_Trait___'` | `False` | enum |
| `'_Trait____'` | `False` | enum |
| `'_Trait_____'` | `False` | enum |
| `'_Trait__x'` | `False` | enum |
| `'_Trait__x_'` | `False` | enum |
| `'_Trait__x__'` | `False` | enum |
| `'_Trait__x___'` | `False` | enum |
| `'_Trait__pkg.mod'` | `False` | enum |
| `'_Trait__pkg.mod_'` | `False` | enum |
| `'_Trait__pkg.mod__'` | `False` | enum |
| `'_Trait__pkg.mod___'` | `False` | enum |
| `'_Target__'` | `False` | enum |
| `'_Target___'` | `False` | enum |
| `'_Target____'` | `False` | enum |
| `'_Target_____'` | `False` | enum |
| `'_Target__x'` | `False` | enum |
| `'_Target__x_'` | `False` | enum |
| `'_Target__x__'` | `False` | enum |
| `'_Target__x___'` | `False` | enum |
| `'_Target__pkg.mod'` | `False` | enum |
| `'_Target__pkg.mod_'` | `False` | enum |
| `'_Target__pkg.mod__'` | `False` | enum |
| `'_Target__pkg.mod___'` | `False` | enum |
| `None` | `raises TypeError` | contract |
| `1` | `raises TypeError` | contract |
| `b'__x__'` | `raises TypeError` ⚠ divergence | contract |

## Found by the generated cases

Calls whose outcome differs from the expectation, kept visible and not yet decided: each case named must keep failing and no other may (the checker reports either).

- `pygim.traits.CodeBuilder: one step`, 18 cases: CodeBuilder respells co_names and qualified names but not a private LOCAL name: co_varnames, co_cellvars and co_freevars keep the old class's spelling (`_Trait__x`), where the method compiled in the target has `_Target__x`. Behaviour is unchanged — locals are found by position, and a cell and its free variable keep matching names — but locals() shows the old spelling, against the docstring's 'respells every private name'.
- `pygim.utils.is_dunder`, 1 case: pygim.utils.is_dunder accepts bytes — pybind11's string caster converts them — though its signature says `name: str`; is_dunder(b'__x__') is True.

## Authored seed and edge rows

Written by hand, one row per behaviour a reader asks about; tested by test_traits.py, test_traits_compose.py and test_utils.py.

### pygim.utils.is_dunder

`is_dunder(name)`

The expected results are Python's own rule, enum._is_dunder; the test also checks every row of this table against enum._is_dunder itself.

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

### pygim.traits.CodeBuilder

`CodeBuilder(code).reown(steps[0].source, steps[0].target).reown(steps[1]...).build()`

`code` is the named function's code object; `result.inner` is the code object nested in its constants (a lambda); `original.co_names` is the code passed in, after build() — a code object is never changed.

co_qualname exists from Python 3.11; on 3.9 and 3.10 the qualified-name columns are not checked.

Besides these literal expectations, every row but `source_not_the_owner` and `other_class_private_chained` is checked against Python's own compiler: the method's source compiled inside a class named like the last target must give the same names.

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

### pygim.traits.compose

`compose(target, traits[0], traits[1], override=override)`

`effect: written` is the names the call set in the target's own namespace, its record `__pygim_traits__` aside; a refused call writes nothing. `effect: traits_of` is list(traits_of(target)) after the call.

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

### pygim.traits.compose: the composed members, called

`compose(target, traits[0], traits[1], override=override), then instance.<member>`

`member` is what is read on an instance of the target: `name()` calls it, `name(other)` calls it with a second instance, a dotted name reads attributes.

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

### pygim.traits.traits_of

`compose(target, traits[0], traits[1], override=override); traits_of(target)[name]`

`expected_result.defined` names the trait member; its expected value is `file:line` of the code that member is made of. `expected_result.composed` is `file:line` of the compose() call.

| target | traits[0] | traits[1] | override | name | expected_result.trait | expected_result.defined | expected_result.composed | case |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `Person` | `Greeter` | — | `False` | `'greet'` | `'traits_cases.Greeter'` | `Greeter.greet` | `the compose() call` | baseline |
| `Person` | `Greeter` | — | `False` | `'name_length'` | `'traits_cases.Greeter'` | `Greeter.name_length` | `the compose() call` | name_property |
| `Person` | `Greeter` | — | `False` | `'kind'` | `'traits_cases.Greeter'` | `Greeter.kind` | `the compose() call` | name_classmethod |
| `Person` | `Greeter` | — | `False` | `'missing'` | `raises KeyError` | — | — | name_not_composed |
| `Person` | `Greeter` | `Loud` | `True` | `'shout'` | `'traits_cases.Loud'` | `Loud.shout` | `the compose() call` | later_trait_recorded |
| `Person` | — | — | `False` | `'greet'` | `raises KeyError` | — | — | nothing_composed |

## What these cases do not cover

- The domains are finite and chosen: every class name, name and qualified name is one of the atoms or generated shapes listed above; no claim is made for strings outside them, nor for classes, code objects or traits beyond what the factories make.
- Expectations for the inputs Python's compiler cannot take (a name or class name that is not an identifier, a qualified name its class does not own) come from the written contract, and the cases say so.
- Two models are reductions, said in their notes: CodeBuilder's two-step chains (one template, the names a step can move) and compose()'s two traits (three member kinds); the one-step and one-trait models cover the rest.
- A trait member written in C++ (the quantity traits to come), composing into a pybind11 class or a class with a metaclass other than Enum's, and composing from several threads have no model.
- On Python 3.9 the frozen slots dataclass target is excluded (dataclass(slots=True) is 3.10's); before 3.11 a code object has no co_qualname, so qualified names are not compared there.
- One-input comparisons explain one input's effect from a baseline; the counts above them are the coverage, interactions included.
- The native product of utils/python_names.h is called and branch-measured only by tests/static/run_python_names_callspace.py, which needs GCC 16 (-freflection); every build proves the same product at compile time. Branch coverage of the pybind11 adapters (traits/compose.h, traits/adapter.h) is not measured. utils/core_utils.h keeps authored tables only.
