<!-- Generated from tests/unittests/traits_cases.py by `python tests/unittests/traits_cases.py`, on Python 3.12;
     tests/unittests/test_traits_cases_page.py fails when it is not current. Edit the models and cases, not this page. -->

# pygim.traits and Python's naming rules: the test cases

Two kinds of case. **Generated** cases are the coverage: each function's call space is described as data — domains of argument values, some chosen by hand (*manual*), most expanded from declared factors and bounds (*generated*) — and expanded into every call; a checker rebuilds the space from the declaration and fails a run that misses, duplicates or adds a call, or whose domain shrank. **Authored** rows, further down, are hand-picked seed and edge cases kept as regression rows and worked examples.

Every expected result comes from Python, never from pygim: `enum._is_dunder`; Python's compiler — a name compiled inside `class <C>:`, a template's qualified name, a trait's members written into the target's own source — or, where the compiler cannot be asked, the written contract. Each model states its expectation sources outside the comparison table.

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

### pygim::python_names::is_dunder / pygim::python_names::is_private_name

`is_dunder(name)` — called by every build and the GCC 16 probe.

`is_private_name(name)` — called by every build and the GCC 16 probe.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `name` | names | generated from shaped names = leading (4, manual) × stem (3, manual) × trailing (4, manual) and names spelt for an owner = owner (2, manual) × body (12, generated) | 63 |

`is_dunder(name)`: product 63, excluded 0; required 63. Expectation sources: enum 63.

`is_private_name(name)`: product 63, excluded 0; required 63. Expectation sources: compiler 62, contract 1.

The first row is the shared baseline. Each later row changes one input from that baseline; the changed cell is bold. All argument columns are shown. These are comparisons drawn from the full generated product counted above, not its complete listing.

| name | expected is_dunder | expected is_private_name |
| --- | --- | --- |
| `'__x__'` | `True` | `False` |
| **`''`** | `False` | `False` |
| **`'_'`** | `False` | `False` |
| **`'__'`** | `False` | `False` |
| **`'___'`** | `False` | `False` |
| **`'x'`** | `False` | `False` |
| **`'x_'`** | `False` | `False` |
| **`'x__'`** | `False` | `False` |
| **`'x___'`** | `False` | `False` |
| **`'pkg.mod'`** | `False` | `False` |
| **`'pkg.mod_'`** | `False` | `False` |
| **`'pkg.mod__'`** | `False` | `False` |
| **`'pkg.mod___'`** | `False` | `False` |
| **`'____'`** | `False` | `False` |
| **`'_x'`** | `False` | `False` |
| **`'_x_'`** | `False` | `False` |
| **`'_x__'`** | `False` | `False` |
| **`'_x___'`** | `False` | `False` |
| **`'_pkg.mod'`** | `False` | `False` |
| **`'_pkg.mod_'`** | `False` | `False` |
| **`'_pkg.mod__'`** | `False` | `False` |
| **`'_pkg.mod___'`** | `False` | `False` |
| **`'_____'`** | `False` | `False` |
| **`'__x'`** | `False` | `True` |
| **`'__x_'`** | `False` | `True` |
| **`'__x___'`** | `False` | `False` |
| **`'__pkg.mod'`** | `False` | `False` |
| **`'__pkg.mod_'`** | `False` | `False` |
| **`'__pkg.mod__'`** | `True` | `False` |
| **`'__pkg.mod___'`** | `False` | `False` |
| **`'______'`** | `False` | `False` |
| **`'___x'`** | `False` | `True` |
| **`'___x_'`** | `False` | `True` |
| **`'___x__'`** | `False` | `False` |
| **`'___x___'`** | `False` | `False` |
| **`'___pkg.mod'`** | `False` | `False` |
| **`'___pkg.mod_'`** | `False` | `False` |
| **`'___pkg.mod__'`** | `False` | `False` |
| **`'___pkg.mod___'`** | `False` | `False` |
| **`'_Trait__'`** | `False` | `False` |
| **`'_Trait___'`** | `False` | `False` |
| **`'_Trait____'`** | `False` | `False` |
| **`'_Trait_____'`** | `False` | `False` |
| **`'_Trait__x'`** | `False` | `False` |
| **`'_Trait__x_'`** | `False` | `False` |
| **`'_Trait__x__'`** | `False` | `False` |
| **`'_Trait__x___'`** | `False` | `False` |
| **`'_Trait__pkg.mod'`** | `False` | `False` |
| **`'_Trait__pkg.mod_'`** | `False` | `False` |
| **`'_Trait__pkg.mod__'`** | `False` | `False` |
| **`'_Trait__pkg.mod___'`** | `False` | `False` |
| **`'_Target__'`** | `False` | `False` |
| **`'_Target___'`** | `False` | `False` |
| **`'_Target____'`** | `False` | `False` |
| **`'_Target_____'`** | `False` | `False` |
| **`'_Target__x'`** | `False` | `False` |
| **`'_Target__x_'`** | `False` | `False` |
| **`'_Target__x__'`** | `False` | `False` |
| **`'_Target__x___'`** | `False` | `False` |
| **`'_Target__pkg.mod'`** | `False` | `False` |
| **`'_Target__pkg.mod_'`** | `False` | `False` |
| **`'_Target__pkg.mod__'`** | `False` | `False` |
| **`'_Target__pkg.mod___'`** | `False` | `False` |

### pygim::python_names::owner_of

`owner_of(class_name)` — called by every build and the GCC 16 probe.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `class_name` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |

`owner_of(class_name)`: product 7, excluded 0; required 7. Expectation sources: compiler 6, contract 1.

The first row is the shared baseline. Each later row changes one input from that baseline; the changed cell is bold. All argument columns are shown. These are comparisons drawn from the full generated product counted above, not its complete listing.

| class_name | expected owner_of |
| --- | --- |
| `'_Trait'` | `'Trait'` |
| **`'Trait'`** | `'Trait'` |
| **`'Target'`** | `'Target'` |
| **`'__Trait'`** | `'Trait'` |
| **`'Trait__'`** | `'Trait__'` |
| **`'__'`** | `''` |
| **`''`** | `''` |

### pygim::python_names::mangled / pygim::python_names::unmangled

`mangled(class_name, name)` — called by every build and the GCC 16 probe.

`unmangled(class_name, name)` — called by every build and the GCC 16 probe.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `class_name` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |
| `name` | names | generated from shaped names = leading (4, manual) × stem (3, manual) × trailing (4, manual) and names spelt for an owner = owner (2, manual) × body (12, generated) | 63 |

`mangled(class_name, name)`: product 441, excluded 0; required 441. Expectation sources: compiler 372, compiler+contract 68, contract 1.

`unmangled(class_name, name)`: product 441, excluded 0; required 441. Expectation sources: compiler 433, compiler+contract 8.

The first row is the shared baseline. Each later row changes one input from that baseline; the changed cell is bold. All argument columns are shown. These are comparisons drawn from the full generated product counted above, not its complete listing.

| class_name | name | expected mangled | expected unmangled |
| --- | --- | --- | --- |
| `'Trait'` | `'__x'` | `'_Trait__x'` | `''` |
| **`'Target'`** | `'__x'` | `'_Target__x'` | `''` |
| **`'_Trait'`** | `'__x'` | `'_Trait__x'` | `''` |
| **`'__Trait'`** | `'__x'` | `'_Trait__x'` | `''` |
| **`'Trait__'`** | `'__x'` | `'_Trait____x'` | `''` |
| **`'__'`** | `'__x'` | `'__x'` | `'__x'` |
| **`''`** | `'__x'` | `'__x'` | `'__x'` |
| `'Trait'` | **`''`** | `''` | `''` |
| `'Trait'` | **`'_'`** | `'_'` | `''` |
| `'Trait'` | **`'__'`** | `'__'` | `''` |
| `'Trait'` | **`'___'`** | `'___'` | `''` |
| `'Trait'` | **`'x'`** | `'x'` | `''` |
| `'Trait'` | **`'x_'`** | `'x_'` | `''` |
| `'Trait'` | **`'x__'`** | `'x__'` | `''` |
| `'Trait'` | **`'x___'`** | `'x___'` | `''` |
| `'Trait'` | **`'pkg.mod'`** | `'pkg.mod'` | `''` |
| `'Trait'` | **`'pkg.mod_'`** | `'pkg.mod_'` | `''` |
| `'Trait'` | **`'pkg.mod__'`** | `'pkg.mod__'` | `''` |
| `'Trait'` | **`'pkg.mod___'`** | `'pkg.mod___'` | `''` |
| `'Trait'` | **`'____'`** | `'____'` | `''` |
| `'Trait'` | **`'_x'`** | `'_x'` | `''` |
| `'Trait'` | **`'_x_'`** | `'_x_'` | `''` |
| `'Trait'` | **`'_x__'`** | `'_x__'` | `''` |
| `'Trait'` | **`'_x___'`** | `'_x___'` | `''` |
| `'Trait'` | **`'_pkg.mod'`** | `'_pkg.mod'` | `''` |
| `'Trait'` | **`'_pkg.mod_'`** | `'_pkg.mod_'` | `''` |
| `'Trait'` | **`'_pkg.mod__'`** | `'_pkg.mod__'` | `''` |
| `'Trait'` | **`'_pkg.mod___'`** | `'_pkg.mod___'` | `''` |
| `'Trait'` | **`'_____'`** | `'_____'` | `''` |
| `'Trait'` | **`'__x_'`** | `'_Trait__x_'` | `''` |
| `'Trait'` | **`'__x__'`** | `'__x__'` | `''` |
| `'Trait'` | **`'__x___'`** | `'__x___'` | `''` |
| `'Trait'` | **`'__pkg.mod'`** | `'__pkg.mod'` | `''` |
| `'Trait'` | **`'__pkg.mod_'`** | `'__pkg.mod_'` | `''` |
| `'Trait'` | **`'__pkg.mod__'`** | `'__pkg.mod__'` | `''` |
| `'Trait'` | **`'__pkg.mod___'`** | `'__pkg.mod___'` | `''` |
| `'Trait'` | **`'______'`** | `'______'` | `''` |
| `'Trait'` | **`'___x'`** | `'_Trait___x'` | `''` |
| `'Trait'` | **`'___x_'`** | `'_Trait___x_'` | `''` |
| `'Trait'` | **`'___x__'`** | `'___x__'` | `''` |
| `'Trait'` | **`'___x___'`** | `'___x___'` | `''` |
| `'Trait'` | **`'___pkg.mod'`** | `'___pkg.mod'` | `''` |
| `'Trait'` | **`'___pkg.mod_'`** | `'___pkg.mod_'` | `''` |
| `'Trait'` | **`'___pkg.mod__'`** | `'___pkg.mod__'` | `''` |
| `'Trait'` | **`'___pkg.mod___'`** | `'___pkg.mod___'` | `''` |
| `'Trait'` | **`'_Trait__'`** | `'_Trait__'` | `''` |
| `'Trait'` | **`'_Trait___'`** | `'_Trait___'` | `''` |
| `'Trait'` | **`'_Trait____'`** | `'_Trait____'` | `''` |
| `'Trait'` | **`'_Trait_____'`** | `'_Trait_____'` | `''` |
| `'Trait'` | **`'_Trait__x'`** | `'_Trait__x'` | `'__x'` |
| `'Trait'` | **`'_Trait__x_'`** | `'_Trait__x_'` | `'__x_'` |
| `'Trait'` | **`'_Trait__x__'`** | `'_Trait__x__'` | `''` |
| `'Trait'` | **`'_Trait__x___'`** | `'_Trait__x___'` | `''` |
| `'Trait'` | **`'_Trait__pkg.mod'`** | `'_Trait__pkg.mod'` | `''` |
| `'Trait'` | **`'_Trait__pkg.mod_'`** | `'_Trait__pkg.mod_'` | `''` |
| `'Trait'` | **`'_Trait__pkg.mod__'`** | `'_Trait__pkg.mod__'` | `''` |
| `'Trait'` | **`'_Trait__pkg.mod___'`** | `'_Trait__pkg.mod___'` | `''` |
| `'Trait'` | **`'_Target__'`** | `'_Target__'` | `''` |
| `'Trait'` | **`'_Target___'`** | `'_Target___'` | `''` |
| `'Trait'` | **`'_Target____'`** | `'_Target____'` | `''` |
| `'Trait'` | **`'_Target_____'`** | `'_Target_____'` | `''` |
| `'Trait'` | **`'_Target__x'`** | `'_Target__x'` | `''` |
| `'Trait'` | **`'_Target__x_'`** | `'_Target__x_'` | `''` |
| `'Trait'` | **`'_Target__x__'`** | `'_Target__x__'` | `''` |
| `'Trait'` | **`'_Target__x___'`** | `'_Target__x___'` | `''` |
| `'Trait'` | **`'_Target__pkg.mod'`** | `'_Target__pkg.mod'` | `''` |
| `'Trait'` | **`'_Target__pkg.mod_'`** | `'_Target__pkg.mod_'` | `''` |
| `'Trait'` | **`'_Target__pkg.mod__'`** | `'_Target__pkg.mod__'` | `''` |
| `'Trait'` | **`'_Target__pkg.mod___'`** | `'_Target__pkg.mod___'` | `''` |

### pygim::python_names::respelt / pygim::python_names::reowned

`respelt(from, to, name)` — called by every build and the GCC 16 probe.

`reowned(from, to, name)` — called by every build and the GCC 16 probe.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `from` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |
| `to` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |
| `name` | names | generated from shaped names = leading (4, manual) × stem (3, manual) × trailing (4, manual) and names spelt for an owner = owner (2, manual) × body (12, generated) | 63 |

`respelt(from, to, name)`: product 3087, excluded 0; required 3087. Expectation sources: compiler 3019, compiler+contract 68.

`reowned(from, to, name)`: product 3087, excluded 0; required 3087. Expectation sources: compiler 3019, compiler+contract 68.

The first row is the shared baseline. Each later row changes one input from that baseline; the changed cell is bold. All argument columns are shown. These are comparisons drawn from the full generated product counted above, not its complete listing.

| from | to | name | expected respelt | expected reowned |
| --- | --- | --- | --- | --- |
| `'Trait'` | `'Target'` | `'_Trait__x'` | `'_Target__x'` | `'_Target__x'` |
| **`'Target'`** | `'Target'` | `'_Trait__x'` | `None` | `'_Trait__x'` |
| **`'_Trait'`** | `'Target'` | `'_Trait__x'` | `'_Target__x'` | `'_Target__x'` |
| **`'__Trait'`** | `'Target'` | `'_Trait__x'` | `'_Target__x'` | `'_Target__x'` |
| **`'Trait__'`** | `'Target'` | `'_Trait__x'` | `None` | `'_Trait__x'` |
| **`'__'`** | `'Target'` | `'_Trait__x'` | `None` | `'_Trait__x'` |
| **`''`** | `'Target'` | `'_Trait__x'` | `None` | `'_Trait__x'` |
| `'Trait'` | **`'Trait'`** | `'_Trait__x'` | `'_Trait__x'` | `'_Trait__x'` |
| `'Trait'` | **`'_Trait'`** | `'_Trait__x'` | `'_Trait__x'` | `'_Trait__x'` |
| `'Trait'` | **`'__Trait'`** | `'_Trait__x'` | `'_Trait__x'` | `'_Trait__x'` |
| `'Trait'` | **`'Trait__'`** | `'_Trait__x'` | `'_Trait____x'` | `'_Trait____x'` |
| `'Trait'` | **`'__'`** | `'_Trait__x'` | `'__x'` | `'__x'` |
| `'Trait'` | **`''`** | `'_Trait__x'` | `'__x'` | `'__x'` |
| `'Trait'` | `'Target'` | **`''`** | `None` | `''` |
| `'Trait'` | `'Target'` | **`'_'`** | `None` | `'_'` |
| `'Trait'` | `'Target'` | **`'__'`** | `None` | `'__'` |
| `'Trait'` | `'Target'` | **`'___'`** | `None` | `'___'` |
| `'Trait'` | `'Target'` | **`'x'`** | `None` | `'x'` |
| `'Trait'` | `'Target'` | **`'x_'`** | `None` | `'x_'` |
| `'Trait'` | `'Target'` | **`'x__'`** | `None` | `'x__'` |
| `'Trait'` | `'Target'` | **`'x___'`** | `None` | `'x___'` |
| `'Trait'` | `'Target'` | **`'pkg.mod'`** | `None` | `'pkg.mod'` |
| `'Trait'` | `'Target'` | **`'pkg.mod_'`** | `None` | `'pkg.mod_'` |
| `'Trait'` | `'Target'` | **`'pkg.mod__'`** | `None` | `'pkg.mod__'` |
| `'Trait'` | `'Target'` | **`'pkg.mod___'`** | `None` | `'pkg.mod___'` |
| `'Trait'` | `'Target'` | **`'____'`** | `None` | `'____'` |
| `'Trait'` | `'Target'` | **`'_x'`** | `None` | `'_x'` |
| `'Trait'` | `'Target'` | **`'_x_'`** | `None` | `'_x_'` |
| `'Trait'` | `'Target'` | **`'_x__'`** | `None` | `'_x__'` |
| `'Trait'` | `'Target'` | **`'_x___'`** | `None` | `'_x___'` |
| `'Trait'` | `'Target'` | **`'_pkg.mod'`** | `None` | `'_pkg.mod'` |
| `'Trait'` | `'Target'` | **`'_pkg.mod_'`** | `None` | `'_pkg.mod_'` |
| `'Trait'` | `'Target'` | **`'_pkg.mod__'`** | `None` | `'_pkg.mod__'` |
| `'Trait'` | `'Target'` | **`'_pkg.mod___'`** | `None` | `'_pkg.mod___'` |
| `'Trait'` | `'Target'` | **`'_____'`** | `None` | `'_____'` |
| `'Trait'` | `'Target'` | **`'__x'`** | `None` | `'__x'` |
| `'Trait'` | `'Target'` | **`'__x_'`** | `None` | `'__x_'` |
| `'Trait'` | `'Target'` | **`'__x__'`** | `None` | `'__x__'` |
| `'Trait'` | `'Target'` | **`'__x___'`** | `None` | `'__x___'` |
| `'Trait'` | `'Target'` | **`'__pkg.mod'`** | `None` | `'__pkg.mod'` |
| `'Trait'` | `'Target'` | **`'__pkg.mod_'`** | `None` | `'__pkg.mod_'` |
| `'Trait'` | `'Target'` | **`'__pkg.mod__'`** | `None` | `'__pkg.mod__'` |
| `'Trait'` | `'Target'` | **`'__pkg.mod___'`** | `None` | `'__pkg.mod___'` |
| `'Trait'` | `'Target'` | **`'______'`** | `None` | `'______'` |
| `'Trait'` | `'Target'` | **`'___x'`** | `None` | `'___x'` |
| `'Trait'` | `'Target'` | **`'___x_'`** | `None` | `'___x_'` |
| `'Trait'` | `'Target'` | **`'___x__'`** | `None` | `'___x__'` |
| `'Trait'` | `'Target'` | **`'___x___'`** | `None` | `'___x___'` |
| `'Trait'` | `'Target'` | **`'___pkg.mod'`** | `None` | `'___pkg.mod'` |
| `'Trait'` | `'Target'` | **`'___pkg.mod_'`** | `None` | `'___pkg.mod_'` |
| `'Trait'` | `'Target'` | **`'___pkg.mod__'`** | `None` | `'___pkg.mod__'` |
| `'Trait'` | `'Target'` | **`'___pkg.mod___'`** | `None` | `'___pkg.mod___'` |
| `'Trait'` | `'Target'` | **`'_Trait__'`** | `None` | `'_Trait__'` |
| `'Trait'` | `'Target'` | **`'_Trait___'`** | `None` | `'_Trait___'` |
| `'Trait'` | `'Target'` | **`'_Trait____'`** | `None` | `'_Trait____'` |
| `'Trait'` | `'Target'` | **`'_Trait_____'`** | `None` | `'_Trait_____'` |
| `'Trait'` | `'Target'` | **`'_Trait__x_'`** | `'_Target__x_'` | `'_Target__x_'` |
| `'Trait'` | `'Target'` | **`'_Trait__x__'`** | `None` | `'_Trait__x__'` |
| `'Trait'` | `'Target'` | **`'_Trait__x___'`** | `None` | `'_Trait__x___'` |
| `'Trait'` | `'Target'` | **`'_Trait__pkg.mod'`** | `None` | `'_Trait__pkg.mod'` |
| `'Trait'` | `'Target'` | **`'_Trait__pkg.mod_'`** | `None` | `'_Trait__pkg.mod_'` |
| `'Trait'` | `'Target'` | **`'_Trait__pkg.mod__'`** | `None` | `'_Trait__pkg.mod__'` |
| `'Trait'` | `'Target'` | **`'_Trait__pkg.mod___'`** | `None` | `'_Trait__pkg.mod___'` |
| `'Trait'` | `'Target'` | **`'_Target__'`** | `None` | `'_Target__'` |
| `'Trait'` | `'Target'` | **`'_Target___'`** | `None` | `'_Target___'` |
| `'Trait'` | `'Target'` | **`'_Target____'`** | `None` | `'_Target____'` |
| `'Trait'` | `'Target'` | **`'_Target_____'`** | `None` | `'_Target_____'` |
| `'Trait'` | `'Target'` | **`'_Target__x'`** | `None` | `'_Target__x'` |
| `'Trait'` | `'Target'` | **`'_Target__x_'`** | `None` | `'_Target__x_'` |
| `'Trait'` | `'Target'` | **`'_Target__x__'`** | `None` | `'_Target__x__'` |
| `'Trait'` | `'Target'` | **`'_Target__x___'`** | `None` | `'_Target__x___'` |
| `'Trait'` | `'Target'` | **`'_Target__pkg.mod'`** | `None` | `'_Target__pkg.mod'` |
| `'Trait'` | `'Target'` | **`'_Target__pkg.mod_'`** | `None` | `'_Target__pkg.mod_'` |
| `'Trait'` | `'Target'` | **`'_Target__pkg.mod__'`** | `None` | `'_Target__pkg.mod__'` |
| `'Trait'` | `'Target'` | **`'_Target__pkg.mod___'`** | `None` | `'_Target__pkg.mod___'` |

### pygim::python_names::requalified

`requalified(from, to, qualname)` — called by every build and the GCC 16 probe.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `from` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |
| `to` | class names | manual: a plain name and a second one to move to; one and two leading underscores, which mangling drops; trailing underscores, which it keeps; only underscores, which mangle nothing; and the empty string, which is no class name at all | 7 |
| `qualname` | qualified names | generated from owned qualified names = owner (7, manual) × tail (4, manual) and unowned qualified names (5, manual) | 33 |

`requalified(from, to, qualname)`: product 1617, excluded 0; required 1617. Expectation sources: compiler 144, contract 1473.

The first row is the shared baseline. Each later row changes one input from that baseline; the changed cell is bold. All argument columns are shown. These are comparisons drawn from the full generated product counted above, not its complete listing.

| from | to | qualname | expected requalified |
| --- | --- | --- | --- |
| `'Trait'` | `'Target'` | `'Trait.f'` | `'Target.f'` |
| **`'Target'`** | `'Target'` | `'Trait.f'` | `'Trait.f'` |
| **`'_Trait'`** | `'Target'` | `'Trait.f'` | `'Trait.f'` |
| **`'__Trait'`** | `'Target'` | `'Trait.f'` | `'Trait.f'` |
| **`'Trait__'`** | `'Target'` | `'Trait.f'` | `'Trait.f'` |
| **`'__'`** | `'Target'` | `'Trait.f'` | `'Trait.f'` |
| **`''`** | `'Target'` | `'Trait.f'` | `'Trait.f'` |
| `'Trait'` | **`'Trait'`** | `'Trait.f'` | `'Trait.f'` |
| `'Trait'` | **`'_Trait'`** | `'Trait.f'` | `'_Trait.f'` |
| `'Trait'` | **`'__Trait'`** | `'Trait.f'` | `'__Trait.f'` |
| `'Trait'` | **`'Trait__'`** | `'Trait.f'` | `'Trait__.f'` |
| `'Trait'` | **`'__'`** | `'Trait.f'` | `'__.f'` |
| `'Trait'` | **`''`** | `'Trait.f'` | `'.f'` |
| `'Trait'` | `'Target'` | **`'Trait.f.<locals>.<lambda>'`** | `'Target.f.<locals>.<lambda>'` |
| `'Trait'` | `'Target'` | **`'Trait.f.<locals>.g'`** | `'Target.f.<locals>.g'` |
| `'Trait'` | `'Target'` | **`'Trait.Inner.f'`** | `'Target.Inner.f'` |
| `'Trait'` | `'Target'` | **`'Target.f'`** | `'Target.f'` |
| `'Trait'` | `'Target'` | **`'Target.f.<locals>.<lambda>'`** | `'Target.f.<locals>.<lambda>'` |
| `'Trait'` | `'Target'` | **`'Target.f.<locals>.g'`** | `'Target.f.<locals>.g'` |
| `'Trait'` | `'Target'` | **`'Target.Inner.f'`** | `'Target.Inner.f'` |
| `'Trait'` | `'Target'` | **`'_Trait.f'`** | `'_Trait.f'` |
| `'Trait'` | `'Target'` | **`'_Trait.f.<locals>.<lambda>'`** | `'_Trait.f.<locals>.<lambda>'` |
| `'Trait'` | `'Target'` | **`'_Trait.f.<locals>.g'`** | `'_Trait.f.<locals>.g'` |
| `'Trait'` | `'Target'` | **`'_Trait.Inner.f'`** | `'_Trait.Inner.f'` |
| `'Trait'` | `'Target'` | **`'__Trait.f'`** | `'__Trait.f'` |
| `'Trait'` | `'Target'` | **`'__Trait.f.<locals>.<lambda>'`** | `'__Trait.f.<locals>.<lambda>'` |
| `'Trait'` | `'Target'` | **`'__Trait.f.<locals>.g'`** | `'__Trait.f.<locals>.g'` |
| `'Trait'` | `'Target'` | **`'__Trait.Inner.f'`** | `'__Trait.Inner.f'` |
| `'Trait'` | `'Target'` | **`'Trait__.f'`** | `'Trait__.f'` |
| `'Trait'` | `'Target'` | **`'Trait__.f.<locals>.<lambda>'`** | `'Trait__.f.<locals>.<lambda>'` |
| `'Trait'` | `'Target'` | **`'Trait__.f.<locals>.g'`** | `'Trait__.f.<locals>.g'` |
| `'Trait'` | `'Target'` | **`'Trait__.Inner.f'`** | `'Trait__.Inner.f'` |
| `'Trait'` | `'Target'` | **`'__.f'`** | `'__.f'` |
| `'Trait'` | `'Target'` | **`'__.f.<locals>.<lambda>'`** | `'__.f.<locals>.<lambda>'` |
| `'Trait'` | `'Target'` | **`'__.f.<locals>.g'`** | `'__.f.<locals>.g'` |
| `'Trait'` | `'Target'` | **`'__.Inner.f'`** | `'__.Inner.f'` |
| `'Trait'` | `'Target'` | **`'.f'`** | `'.f'` |
| `'Trait'` | `'Target'` | **`'.f.<locals>.<lambda>'`** | `'.f.<locals>.<lambda>'` |
| `'Trait'` | `'Target'` | **`'.f.<locals>.g'`** | `'.f.<locals>.g'` |
| `'Trait'` | `'Target'` | **`'.Inner.f'`** | `'.Inner.f'` |
| `'Trait'` | `'Target'` | **`'f'`** | `'f'` |
| `'Trait'` | `'Target'` | **`'TraitX.f'`** | `'TraitX.f'` |
| `'Trait'` | `'Target'` | **`'Trait'`** | `'Trait'` |
| `'Trait'` | `'Target'` | **`'Trait.'`** | `'Target.'` |
| `'Trait'` | `'Target'` | **`''`** | `''` |

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

`CodeBuilder(template(name) compiled in class owner).reown(*step).build()`: product 3276, excluded 390 — a dotted name is written only by an import (390); required 2886. Expectation sources: compiler 2886.

The first row is the shared baseline. Each later row changes one input from that baseline; the changed cell is bold. All argument columns are shown. These are comparisons drawn from the full generated product counted above, not its complete listing.

| template | name | owner | step | expected pygim.traits.CodeBuilder: one step |
| --- | --- | --- | --- | --- |
| `'attribute'` | `'__x'` | `'Trait'` | `('Trait', 'Target')` | `('_Target__x',) Target.f` |
| **`'global'`** | `'__x'` | `'Trait'` | `('Trait', 'Target')` | `('_Target__x',) Target.f` |
| **`'lambda'`** | `'__x'` | `'Trait'` | `('Trait', 'Target')` | `() Target.f` · `('_Target__x',) Target.f.<locals>.<lambda>` |
| **`'nested def'`** | `'__x'` | `'Trait'` | `('Trait', 'Target')` | `() Target.f` · `('_Target__x',) Target.f.<locals>.g` |
| **`'import'`** | `'__x'` | `'Trait'` | `('Trait', 'Target')` | `('_Target__x',) Target.f` ⚠ divergence |
| **`'local'`** | `'__x'` | `'Trait'` | `('Trait', 'Target')` | `() Target.f` ⚠ divergence |
| `'attribute'` | **`'__x__'`** | `'Trait'` | `('Trait', 'Target')` | `('__x__',) Target.f` |
| `'attribute'` | **`'_x'`** | `'Trait'` | `('Trait', 'Target')` | `('_x',) Target.f` |
| `'attribute'` | **`'x'`** | `'Trait'` | `('Trait', 'Target')` | `('x',) Target.f` |
| `'attribute'` | **`'_Trait__x'`** | `'Trait'` | `('Trait', 'Target')` | `('_Target__x',) Target.f` |
| `'attribute'` | **`'_Target__x'`** | `'Trait'` | `('Trait', 'Target')` | `('_Target__x',) Target.f` |
| `'attribute'` | `'__x'` | **`'_Trait'`** | `('Trait', 'Target')` | `('_Target__x',) _Trait.f` |
| `'attribute'` | `'__x'` | **`'__'`** | `('Trait', 'Target')` | `('__x',) __.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`None`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('Trait', 'Trait')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('Trait', '_Trait')`** | `('_Trait__x',) _Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('Trait', '__')`** | `('__x',) __.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('Trait', 'Outer.Target')`** | `('_Target__x',) Outer.Target.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('_Trait', 'Trait')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('_Trait', '_Trait')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('_Trait', '__')`** | `('__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('_Trait', 'Target')`** | `('_Target__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('_Trait', 'Outer.Target')`** | `('_Target__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('__', 'Trait')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('__', '_Trait')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('__', '__')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('__', 'Target')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('__', 'Outer.Target')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('Target', 'Trait')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('Target', '_Trait')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('Target', '__')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('Target', 'Target')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('Target', 'Outer.Target')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('Outer.Target', 'Trait')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('Outer.Target', '_Trait')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('Outer.Target', '__')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('Outer.Target', 'Target')`** | `('_Trait__x',) Trait.f` |
| `'attribute'` | `'__x'` | `'Trait'` | **`('Outer.Target', 'Outer.Target')`** | `('_Trait__x',) Trait.f` |

### pygim.traits.CodeBuilder: two steps

`CodeBuilder(attribute(name) compiled in class owner).reown(*steps[0]).reown(*steps[1]).build()` — called by pytest.

A reduction: every chain of two steps, crossed with the attribute template only and with the names a step can move. build() applies the same move to every name of every code object in the tree, which the one-step model shows over every template and kind of name.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `name` | names a chain can move | manual: the names a step can respell, and a public one it never does | 4 |
| `owner` | owners of the code | manual: the class the code is compiled in: a plain name, a leading underscore mangling drops, only underscores (which mangle nothing) | 3 |
| `steps[0]` | steps | generated: steps = source (5, manual) × target (5, manual) | 25 |
| `steps[1]` | steps | generated: steps = source (5, manual) × target (5, manual) | 25 |

`CodeBuilder(attribute(name) compiled in class owner).reown(*steps[0]).reown(*steps[1]).build()`: product 7500, excluded 0; required 7500. Expectation sources: compiler 7500.

The first row is the shared baseline. Each later row changes one input from that baseline; the changed cell is bold. All argument columns are shown. These are comparisons drawn from the full generated product counted above, not its complete listing.

| name | owner | steps[0] | steps[1] | expected pygim.traits.CodeBuilder: two steps |
| --- | --- | --- | --- | --- |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | `('Target', 'Outer.Target')` | `('_Target__x',) Outer.Target.f` |
| **`'_Trait__x'`** | `'Trait'` | `('Trait', 'Target')` | `('Target', 'Outer.Target')` | `('_Target__x',) Outer.Target.f` |
| **`'_Target__x'`** | `'Trait'` | `('Trait', 'Target')` | `('Target', 'Outer.Target')` | `('_Target__x',) Outer.Target.f` |
| **`'x'`** | `'Trait'` | `('Trait', 'Target')` | `('Target', 'Outer.Target')` | `('x',) Outer.Target.f` |
| `'__x'` | **`'_Trait'`** | `('Trait', 'Target')` | `('Target', 'Outer.Target')` | `('_Target__x',) _Trait.f` |
| `'__x'` | **`'__'`** | `('Trait', 'Target')` | `('Target', 'Outer.Target')` | `('__x',) __.f` |
| `'__x'` | `'Trait'` | **`('Trait', 'Trait')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('Trait', '_Trait')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) _Trait.f` |
| `'__x'` | `'Trait'` | **`('Trait', '__')`** | `('Target', 'Outer.Target')` | `('__x',) __.f` |
| `'__x'` | `'Trait'` | **`('Trait', 'Outer.Target')`** | `('Target', 'Outer.Target')` | `('_Target__x',) Outer.Target.f` |
| `'__x'` | `'Trait'` | **`('_Trait', 'Trait')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('_Trait', '_Trait')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('_Trait', '__')`** | `('Target', 'Outer.Target')` | `('__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('_Trait', 'Target')`** | `('Target', 'Outer.Target')` | `('_Target__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('_Trait', 'Outer.Target')`** | `('Target', 'Outer.Target')` | `('_Target__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('__', 'Trait')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('__', '_Trait')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('__', '__')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('__', 'Target')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('__', 'Outer.Target')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('Target', 'Trait')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('Target', '_Trait')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('Target', '__')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('Target', 'Target')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('Target', 'Outer.Target')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('Outer.Target', 'Trait')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('Outer.Target', '_Trait')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('Outer.Target', '__')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('Outer.Target', 'Target')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | **`('Outer.Target', 'Outer.Target')`** | `('Target', 'Outer.Target')` | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('Trait', 'Trait')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('Trait', '_Trait')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('Trait', '__')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('Trait', 'Target')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('Trait', 'Outer.Target')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('_Trait', 'Trait')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('_Trait', '_Trait')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('_Trait', '__')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('_Trait', 'Target')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('_Trait', 'Outer.Target')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('__', 'Trait')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('__', '_Trait')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('__', '__')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('__', 'Target')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('__', 'Outer.Target')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('Target', 'Trait')`** | `('_Trait__x',) Trait.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('Target', '_Trait')`** | `('_Trait__x',) _Trait.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('Target', '__')`** | `('__x',) __.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('Target', 'Target')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('Outer.Target', 'Trait')`** | `('_Trait__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('Outer.Target', '_Trait')`** | `('_Trait__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('Outer.Target', '__')`** | `('__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('Outer.Target', 'Target')`** | `('_Target__x',) Target.f` |
| `'__x'` | `'Trait'` | `('Trait', 'Target')` | **`('Outer.Target', 'Outer.Target')`** | `('_Target__x',) Target.f` |

### pygim.traits.CodeBuilder: refusals

`CodeBuilder(code).reown(source, target), one argument wrong` — called by pytest.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `position` | the argument that is wrong | manual: each argument CodeBuilder takes, wrong alone | 3 |
| `wrong` | wrong arguments | manual: source text instead of code, the function instead of its code, None, an instance instead of its class | 4 |

`CodeBuilder(code).reown(source, target), one argument wrong`: product 12, excluded 0; required 12. Expectation sources: contract 12.

The first row is the shared baseline. Each later row changes one input from that baseline; the changed cell is bold. All argument columns are shown. These are comparisons drawn from the full generated product counted above, not its complete listing.

| position | wrong | expected pygim.traits.CodeBuilder: refusals |
| --- | --- | --- |
| `'code'` | `'text'` | `raises TypeError: CodeBuilder takes a code object, not str` |
| **`'source'`** | `'text'` | `raises TypeError` |
| **`'target'`** | `'text'` | `raises TypeError` |
| `'code'` | **`'function'`** | `raises TypeError: CodeBuilder takes a code object, not function` |
| `'code'` | **`'none'`** | `raises TypeError: CodeBuilder takes a code object, not NoneType` |
| `'code'` | **`'instance'`** | `raises TypeError: CodeBuilder takes a code object, not Target` |

### pygim.traits.compose: one trait

`compose(target, *traits[:1], override=override); traits_of(target)` — called by pytest.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `target` | targets | manual: a plain class; one with a base; one that defines a name a trait has; a frozen slots dataclass (rebuilt by its decorator); an Enum (a metaclass of its own); an instance instead of a class | 6 |
| `traits[0]` | one trait or none | generated from none (1, manual) and traits (17, generated) | 18 |
| `override` | override | manual: both | 2 |

`compose(target, *traits[:1], override=override); traits_of(target)`: product 216, excluded 8 — Python itself cannot answer: zero-argument super() written in a slots dataclass raises TypeError (CPython gh-90562), so the member written there is no oracle (8); required 208. Expectation sources: contract 73, contract+compiler 135.

The first row is the shared baseline. Each later row changes one input from that baseline; the changed cell is bold. All argument columns are shown. These are comparisons drawn from the full generated product counted above, not its complete listing.

| target | traits[0] | override | expected pygim.traits.compose: one trait |
| --- | --- | --- | --- |
| `'plain'` | `('function', 'private', 'greet')` | `False` | `writes greet; record greet ← TraitA; greet → 'TraitA:ann' (Person.greet)` |
| **`'with a base'`** | `('function', 'private', 'greet')` | `False` | `writes greet; record greet ← TraitA; greet → 'TraitA:ann' (Child.greet)` |
| **`'with greet'`** | `('function', 'private', 'greet')` | `False` | `raises ValueError: Person already has greet; traits_callspace.generated.TraitA would replace it (pass override=True)` |
| **`'frozen slots dataclass'`** | `('function', 'private', 'greet')` | `False` | `writes greet; record greet ← TraitA; greet → raises AttributeError (Point.greet)` |
| **`'enum'`** | `('function', 'private', 'greet')` | `False` | `writes greet; record greet ← TraitA; greet → raises AttributeError (Colour.greet)` |
| **`'not a class'`** | `('function', 'private', 'greet')` | `False` | `raises TypeError` |
| `'plain'` | **`None`** | `False` | `writes nothing` |
| `'plain'` | **`('function', 'plain', 'greet')`** | `False` | `writes greet; record greet ← TraitA; greet → 'TraitA' (Person.greet)` |
| `'plain'` | **`('function', 'plain', 'hello')`** | `False` | `writes hello; record hello ← TraitA; hello → 'TraitA' (Person.hello)` |
| `'plain'` | **`('function', 'private', 'hello')`** | `False` | `writes hello; record hello ← TraitA; hello → 'TraitA:ann' (Person.hello)` |
| `'plain'` | **`('function', 'super', 'greet')`** | `False` | `writes greet; record greet ← TraitA; greet → raises AttributeError (Person.greet)` |
| `'plain'` | **`('function', 'super', 'hello')`** | `False` | `writes hello; record hello ← TraitA; hello → raises AttributeError (Person.hello)` |
| `'plain'` | **`('property', 'private', 'greet')`** | `False` | `writes greet; record greet ← TraitA; greet → 'TraitA:ann' (Person.greet)` |
| `'plain'` | **`('property', 'private', 'hello')`** | `False` | `writes hello; record hello ← TraitA; hello → 'TraitA:ann' (Person.hello)` |
| `'plain'` | **`('property', 'super', 'greet')`** | `False` | `writes greet; record greet ← TraitA; greet → raises AttributeError (Person.greet)` |
| `'plain'` | **`('property', 'super', 'hello')`** | `False` | `writes hello; record hello ← TraitA; hello → raises AttributeError (Person.hello)` |
| `'plain'` | **`('classmethod', 'private', 'greet')`** | `False` | `writes greet; record greet ← TraitA; greet → 'TraitA:person' (Person.greet)` |
| `'plain'` | **`('classmethod', 'private', 'hello')`** | `False` | `writes hello; record hello ← TraitA; hello → 'TraitA:person' (Person.hello)` |
| `'plain'` | **`('staticmethod', 'plain', 'greet')`** | `False` | `writes greet; record greet ← TraitA; greet → 'TraitA' (Person.greet)` |
| `'plain'` | **`('staticmethod', 'plain', 'hello')`** | `False` | `writes hello; record hello ← TraitA; hello → 'TraitA' (Person.hello)` |
| `'plain'` | **`('constant', 'plain', 'greet')`** | `False` | `raises TypeError: TraitA.greet is int; a trait composes functions, properties, classmethods and staticmethods` |
| `'plain'` | **`('constant', 'plain', 'hello')`** | `False` | `raises TypeError: TraitA.hello is int; a trait composes functions, properties, classmethods and staticmethods` |
| `'plain'` | **`'not a class'`** | `False` | `raises TypeError: compose takes classes as traits, not function` |
| `'plain'` | `('function', 'private', 'greet')` | **`True`** | `writes greet; record greet ← TraitA; greet → 'TraitA:ann' (Person.greet)` |

### pygim.traits.compose: two traits

`compose(target, traits[0], traits[1], override=override); traits_of(target)` — called by pytest.

A reduction: a second trait meets the first through its names (a conflict, which trait wins, what the record says) and its kind (a refusal stops the call), so two traits are crossed over a function, a property and a constant under each name; every member body is crossed with every target in the one-trait model.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `target` | targets | manual: a plain class; one with a base; one that defines a name a trait has; a frozen slots dataclass (rebuilt by its decorator); an Enum (a metaclass of its own); an instance instead of a class | 6 |
| `traits[0]` | traits, for two | generated from trait classes = member (3, manual) × name (2, manual) and not a trait (1, manual) | 7 |
| `traits[1]` | traits, for two | generated from trait classes = member (3, manual) × name (2, manual) and not a trait (1, manual) | 7 |
| `override` | override | manual: both | 2 |

`compose(target, traits[0], traits[1], override=override); traits_of(target)`: product 588, excluded 0; required 588. Expectation sources: contract 476, contract+compiler 112.

The first row is the shared baseline. Each later row changes one input from that baseline; the changed cell is bold. All argument columns are shown. These are comparisons drawn from the full generated product counted above, not its complete listing.

| target | traits[0] | traits[1] | override | expected pygim.traits.compose: two traits |
| --- | --- | --- | --- | --- |
| `'plain'` | `('function', 'private', 'greet')` | `('function', 'private', 'hello')` | `False` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → 'TraitA:ann' (Person.greet); hello → 'TraitB:ann' (Person.hello)` |
| **`'with a base'`** | `('function', 'private', 'greet')` | `('function', 'private', 'hello')` | `False` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → 'TraitA:ann' (Child.greet); hello → 'TraitB:ann' (Child.hello)` |
| **`'with greet'`** | `('function', 'private', 'greet')` | `('function', 'private', 'hello')` | `False` | `raises ValueError: Person already has greet; traits_callspace.generated.TraitA would replace it (pass override=True)` |
| **`'frozen slots dataclass'`** | `('function', 'private', 'greet')` | `('function', 'private', 'hello')` | `False` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → raises AttributeError (Point.greet); hello → raises AttributeError (Point.hello)` |
| **`'enum'`** | `('function', 'private', 'greet')` | `('function', 'private', 'hello')` | `False` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → raises AttributeError (Colour.greet); hello → raises AttributeError (Colour.hello)` |
| **`'not a class'`** | `('function', 'private', 'greet')` | `('function', 'private', 'hello')` | `False` | `raises TypeError` |
| `'plain'` | **`('function', 'private', 'hello')`** | `('function', 'private', 'hello')` | `False` | `raises ValueError: traits_callspace.generated.TraitA and traits_callspace.generated.TraitB both define hello (pass override=True to let the later win)` |
| `'plain'` | **`('property', 'private', 'greet')`** | `('function', 'private', 'hello')` | `False` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → 'TraitA:ann' (Person.greet); hello → 'TraitB:ann' (Person.hello)` |
| `'plain'` | **`('property', 'private', 'hello')`** | `('function', 'private', 'hello')` | `False` | `raises ValueError: traits_callspace.generated.TraitA and traits_callspace.generated.TraitB both define hello (pass override=True to let the later win)` |
| `'plain'` | **`('constant', 'plain', 'greet')`** | `('function', 'private', 'hello')` | `False` | `raises TypeError: TraitA.greet is int; a trait composes functions, properties, classmethods and staticmethods` |
| `'plain'` | **`('constant', 'plain', 'hello')`** | `('function', 'private', 'hello')` | `False` | `raises TypeError: TraitA.hello is int; a trait composes functions, properties, classmethods and staticmethods` |
| `'plain'` | **`'not a class'`** | `('function', 'private', 'hello')` | `False` | `raises TypeError: compose takes classes as traits, not function` |
| `'plain'` | `('function', 'private', 'greet')` | **`('function', 'private', 'greet')`** | `False` | `raises ValueError: traits_callspace.generated.TraitA and traits_callspace.generated.TraitB both define greet (pass override=True to let the later win)` |
| `'plain'` | `('function', 'private', 'greet')` | **`('property', 'private', 'greet')`** | `False` | `raises ValueError: traits_callspace.generated.TraitA and traits_callspace.generated.TraitB both define greet (pass override=True to let the later win)` |
| `'plain'` | `('function', 'private', 'greet')` | **`('property', 'private', 'hello')`** | `False` | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → 'TraitA:ann' (Person.greet); hello → 'TraitB:ann' (Person.hello)` |
| `'plain'` | `('function', 'private', 'greet')` | **`('constant', 'plain', 'greet')`** | `False` | `raises TypeError: TraitB.greet is int; a trait composes functions, properties, classmethods and staticmethods` |
| `'plain'` | `('function', 'private', 'greet')` | **`('constant', 'plain', 'hello')`** | `False` | `raises TypeError: TraitB.hello is int; a trait composes functions, properties, classmethods and staticmethods` |
| `'plain'` | `('function', 'private', 'greet')` | **`'not a class'`** | `False` | `raises TypeError: compose takes classes as traits, not function` |
| `'plain'` | `('function', 'private', 'greet')` | `('function', 'private', 'hello')` | **`True`** | `writes greet, hello; record greet ← TraitA, hello ← TraitB; greet → 'TraitA:ann' (Person.greet); hello → 'TraitB:ann' (Person.hello)` |

### pygim.utils.is_dunder

`is_dunder(name)` — called by pytest.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `name` | names and not names | generated from names (63, generated) and not text (3, manual) | 66 |

`is_dunder(name)`: product 66, excluded 0; required 66. Expectation sources: contract 3, enum 63.

The first row is the shared baseline. Each later row changes one input from that baseline; the changed cell is bold. All argument columns are shown. These are comparisons drawn from the full generated product counted above, not its complete listing.

| name | expected pygim.utils.is_dunder |
| --- | --- |
| `'__x__'` | `True` |
| **`''`** | `False` |
| **`'_'`** | `False` |
| **`'__'`** | `False` |
| **`'___'`** | `False` |
| **`'x'`** | `False` |
| **`'x_'`** | `False` |
| **`'x__'`** | `False` |
| **`'x___'`** | `False` |
| **`'pkg.mod'`** | `False` |
| **`'pkg.mod_'`** | `False` |
| **`'pkg.mod__'`** | `False` |
| **`'pkg.mod___'`** | `False` |
| **`'____'`** | `False` |
| **`'_x'`** | `False` |
| **`'_x_'`** | `False` |
| **`'_x__'`** | `False` |
| **`'_x___'`** | `False` |
| **`'_pkg.mod'`** | `False` |
| **`'_pkg.mod_'`** | `False` |
| **`'_pkg.mod__'`** | `False` |
| **`'_pkg.mod___'`** | `False` |
| **`'_____'`** | `False` |
| **`'__x'`** | `False` |
| **`'__x_'`** | `False` |
| **`'__x___'`** | `False` |
| **`'__pkg.mod'`** | `False` |
| **`'__pkg.mod_'`** | `False` |
| **`'__pkg.mod__'`** | `True` |
| **`'__pkg.mod___'`** | `False` |
| **`'______'`** | `False` |
| **`'___x'`** | `False` |
| **`'___x_'`** | `False` |
| **`'___x__'`** | `False` |
| **`'___x___'`** | `False` |
| **`'___pkg.mod'`** | `False` |
| **`'___pkg.mod_'`** | `False` |
| **`'___pkg.mod__'`** | `False` |
| **`'___pkg.mod___'`** | `False` |
| **`'_Trait__'`** | `False` |
| **`'_Trait___'`** | `False` |
| **`'_Trait____'`** | `False` |
| **`'_Trait_____'`** | `False` |
| **`'_Trait__x'`** | `False` |
| **`'_Trait__x_'`** | `False` |
| **`'_Trait__x__'`** | `False` |
| **`'_Trait__x___'`** | `False` |
| **`'_Trait__pkg.mod'`** | `False` |
| **`'_Trait__pkg.mod_'`** | `False` |
| **`'_Trait__pkg.mod__'`** | `False` |
| **`'_Trait__pkg.mod___'`** | `False` |
| **`'_Target__'`** | `False` |
| **`'_Target___'`** | `False` |
| **`'_Target____'`** | `False` |
| **`'_Target_____'`** | `False` |
| **`'_Target__x'`** | `False` |
| **`'_Target__x_'`** | `False` |
| **`'_Target__x__'`** | `False` |
| **`'_Target__x___'`** | `False` |
| **`'_Target__pkg.mod'`** | `False` |
| **`'_Target__pkg.mod_'`** | `False` |
| **`'_Target__pkg.mod__'`** | `False` |
| **`'_Target__pkg.mod___'`** | `False` |
| **`None`** | `raises TypeError` |
| **`1`** | `raises TypeError` |
| **`b'__x__'`** | `raises TypeError` ⚠ divergence |

## Found by the generated cases

Calls whose outcome differs from the expectation, kept visible and not yet decided: each case named must keep failing and no other may (the checker reports either).

- `pygim.traits.CodeBuilder: one step`, 18 cases: CodeBuilder respells co_names and qualified names but not a private LOCAL name: co_varnames, co_cellvars and co_freevars keep the old class's spelling (`_Trait__x`), where the method compiled in the target has `_Target__x`. Behaviour is unchanged — locals are found by position, and a cell and its free variable keep matching names — but locals() shows the old spelling, against the docstring's 'respells every private name'.
- `pygim.utils.is_dunder`, 1 case: pygim.utils.is_dunder accepts bytes — pybind11's string caster converts them — though its signature says `name: str`; is_dunder(b'__x__') is True.

## Adversarial cases and state preservation

Hostile and unusual inputs, generated from declared domains (traits_adversarial.py) and run by test_traits_adversarial.py. These APIs manipulate executable Python objects and are not a sandbox for arbitrary Python; the guarantees checked are native safety, only the documented state changing on success, nothing changing on a documented refusal, and no stale bookkeeping after either. A legitimate name is never refused for looking unusual. State is read by trusted projections — names and object identity, never a value's `__eq__`, `__repr__` or `__hash__` — with sentinels on unrelated classes.

`compose(target, Trait{name: member}, override=override); traits_of(target)`

Each call composes one member of the given name and kind. The expected outcome is from the documented refusals and Python's own setattr, never from compose(). State is checked by trusted projections (names and identity) with sentinels on unrelated classes.

| parameter | domain | how it is made | values |
| --- | --- | --- | --- |
| `target` | targets | manual: a bare class; one that already defines the baseline name; one with a base, so an inherited name is not mistaken for an owned one | 3 |
| `name` | adversarial names | manual: one of each name a caller might pass that could confuse the write, the conflict check or the record: a NUL that truncates, the record's own name, reserved type slots, over-long and non-ASCII names, and two legitimate names that must pass | 12 |
| `kind` | member kinds | manual: the two kinds whose write path differs; the others reduce to a function inside | 2 |
| `override` | override | manual: both | 2 |

Product 144, excluded 24 — not expressible as a class member (Python forbids the slot) (24); required 120. Every expected outcome is from the contract and Python's own setattr.

| name | outcome | effect on state |
| --- | --- | --- |
| `'greet'` | composed | written whole; only this name and the record change |
| `'__init__'` | composed | written whole; only this name and the record change |
| `'greet\x00x'` | composed | written whole; only this name and the record change |
| `'__pygim_traits__'` | raises ValueError | nothing written; a later composition still works |
| `'__dict__'` | — excluded — | not expressible as a class member |
| `'__class__'` | — excluded — | not expressible as a class member |
| `'__slots__'` | composed | written whole; only this name and the record change |
| `'________________________________________'` | composed | written whole; only this name and the record change |
| `'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx'` | composed | written whole; only this name and the record change |
| `'ɡreet'` | composed | written whole; only this name and the record change |
| `'café'` | composed | written whole; only this name and the record change |
| `'é'` | composed | written whole; only this name and the record change |


### Case families

Every adversarial family and how it is covered: *generated* (a model), *seeded* (authored cases), *subprocess* (a bounded child), *sanitizer* (ASan/UBSan), each with its reason.

| family | covered by | how |
| --- | --- | --- |
| Names: NUL, confusables, combining marks, long underscore runs and names, non-ASCII, reserved type slots, collision with the record name | generated | traits_adversarial.ADVERSARIAL — the name domain; reserved slots Python forbids as members are excluded with that reason, and the record name and owned slots are refusals. |
| Names: non-str keys and lone surrogates | generated+seeded | A non-str key and a surrogate name are refused by compose() (TypeError / UnicodeEncodeError); the surrogate is in the name domain, the non-str key is an authored seed. |
| Identity and aliasing: distinct classes with identical names, repeated traits, an aliased function, inherited conflicts, independent copies per target | seeded | test_traits_adversarial.TestIdentityAndAliasing — these are relationships between whole classes, not one member's name, so they are authored, not generated. |
| Hostile protocol objects: a metaclass __setattr__ that raises or deletes the record mid-write | seeded+subprocess | test_traits_regressions (the record-deleting hook, in a bounded child) and the state model's cleanup obligation. A hook that raises mid-write is the open rollback policy question (docs/design/traits.md), so its partial-write outcome is surfaced, not asserted as a contract. |
| Code objects: nested functions, lambdas, closures, __class__/super cells, private locals vs attributes, reown chains | generated | traits_callspace.py — the CodeBuilder one-step and two-step models and the compose member bodies; a private local name is the known divergence recorded in traits.md. |
| Resource abuse: code nested past the recursion limit, and (bounded) large inputs | subprocess | test_traits_regressions runs the deep-nesting case in a bounded child; it now raises RecursionError instead of crashing. Wide trait sets and many compositions are not yet modelled. |
| Native memory safety of the C++ naming rules | sanitizer | tests/static/run_python_names_sanitizer.py drives utils/python_names.h over a seed corpus with bounded mutations under ASan+UBSan, with a planted-OOB negative control. No coverage-guided fuzzer is installed (no libFuzzer/AFL), so there is no feedback-driven search; this is reported, not faked. |


## Authored seed and edge rows

Written by hand, one row per behaviour a reader asks about; tested by test_traits.py, test_traits_compose.py, test_utils.py and test_traits_regressions.py (the fixed-defect seeds).

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
- The native product of utils/python_names.h is called and branch-measured only by tests/static/run_python_names_callspace.py, which needs GCC 16 (-freflection); every build proves the same product at compile time. Branch coverage of the pybind11 adapters (traits/adapter/compose.h, traits/adapter/code_builder.h) is not measured. utils/core_utils.h keeps authored tables only.
