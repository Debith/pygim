GCC reflection: call space and generated tests
=============================================

The first opt-in experiment selects pygim's real ``pygim::to_csv`` implementation by its
reflection (``^^pygim::to_csv``). No copied function declaration and no C++ parser are
used. Production sources and installed extensions are left unchanged.

Run inside WSL from the pygim repository root::

    ~/miniconda3/envs/dnd/bin/python tests/prototypes/reflection_call_space/run.py

Requirements: pygim and pytest in that existing environment, plus GCC 16 with
reflection and its matching gcov. The driver defaults to ``/usr/bin/g++-16`` and
``/usr/bin/gcov-16``; override with ``--compiler`` and ``--gcov``. It compiles a
standalone executable, not an extension that loads into the conda interpreter.
It is deliberately outside automatic pytest collection and the release build.

What is automatic
-----------------

* Reflection extracts the function name, return type, parameter names and types,
  and whether each parameter has a default.
* Type specializations select reusable value domains: both Boolean values; five
  string atoms; vectors of lengths zero through three over their element domain.
* The generator derives all legal trailing-default omissions, makes the full
  product, and calls the reflected declaration. Declaration splicing lets the
  C++ compiler apply the actual default; no default value is copied into the
  generator and no function pointer loses the default.
* It emits a machine-readable model, concrete calls, results and omission records.
* An independent Python checker reconstructs required calls from the model and
  compares them with the actual records. It rejects gaps, duplicates and empty
  domains. Unsupported reflected parameter types fail C++ compilation.
* The driver emits ordinary parameterized pytest tests. Their native fixture
  executes the compiled probe again; the Python tests call public
  ``pygim.utils.to_csv``. One Python contract supplies both verification and the
  generated tests. The generated file can be rerun with the command in the report.
* Compiler-matched gcov records native branches and condition outcomes.

What is supplied by a person
---------------------------

* The selected function, currently the single ``^^pygim::to_csv`` in ``probe.cpp``.
* The finite string corpus: empty string, ``b``, ``a``, comma-space and ``é``.
* The maximum vector length, three. These are reusable generation policies, not
  information discovered by reflection. They can be changed in ``reflection_values.h``.
* The contract: preserve order unless sorting was requested, join with comma-space,
  and use false as the expected default. This is explicitly specified in
  ``verify.expected_to_csv``; it is not learned from today's implementation.
* The Python binding lookup in ``pygim.utils``. This prototype does not infer a
  general C++-to-Python binding map, overload resolution, ownership, or conversions.

The function joins strings; it is not an RFC CSV quoting/escaping implementation.

Coverage claim and known limits
-------------------------------

There are 1 + 5 + 25 + 125 = 156 vectors. Crossed with sorting false, true or
omitted, these give 468 native calls. This is exhaustive within the declared
finite model, not over every possible string, vector length, allocation outcome
or Python conversion. Reflection exposes declarations, not function bodies.
An ``int`` or a new user-defined type needs a domain policy before it can run.

The first complete run verified all 468 native calls and all 10 recorded condition
outcomes. Raw native branch coverage was 14/18: the four unexercised edges were
marked ``throw`` by gcov at sorting, reserve and append operations. No allocation
fault injection is included, and those edges remain visible as uncovered.

The actual binding has a smaller call space than the C++ declaration:

* Explicit positional arguments: 312 cases verified.
* Omitted Boolean argument: 156 calls rejected by the binding.
* Native parameter names as Python keywords: all 468 calls rejected.

The binding currently declares neither ``py::arg`` names nor a Python default.
These are interface parity findings; this prototype does not change that API or
declare whether exposing native defaults is the desired public contract.
``python_native_call_parity_complete`` is explicitly false in the report. The
780 passing generated tests comprise 468 native calls and 312 supported Python
calls; rejected forms are reported separately, never counted as covered.

Checks against false confidence
-------------------------------

1. Remove one case and adjust the recorded total: the independent checker still
   names the missing call.
2. Add a synthetic function with an unsupported ``double`` parameter: compilation
   must fail with the unmodeled-parameter diagnostic.
3. In a scratch copy of the production header, invert the sorting condition and
   compile it. The same generated call space remains complete, but 276 calls
   violate the contract. The production header is never edited.

Artifacts and timing
--------------------

Every invocation creates a fresh directory under ``build/reflection-call-space``.
``latest.json`` identifies the latest successful run. It contains:

* ``cases.json``: the reflected model and every generated native call/result.
* ``test_to_csv_generated.py``: executable generated tests.
* ``binding-audit.json``: every Python call, result or rejection.
* ``native-coverage.json`` and gcov files: complete native coverage evidence.
* ``mutant/failures.json`` and ``unsupported.log``: negative-control evidence.
* ``report.json`` / ``report.txt``, build logs and pytest output: conclusions,
  compiler version, source hash, commands and timings.

Compilation, native execution/serialization and pytest process time are measured
separately. They are individual-run observations, not a benchmark distribution.
The overall experiment includes three compilations; it is not a subsecond build.
Saved binaries must be regenerated with the driver after changing source or
domains. Replaying a saved suite checks that saved binary and the currently
installed Python binding.

Next extensions
---------------

Add constraints and broader boundary domains for numbers and
containers; explicit state factories and contracts; a binding metadata adapter;
and fault injection for exceptional branches. A covering-array generator can
replace the full product while retaining the independent obligation checker.
Templates and build configurations require a declared supported-instantiation
matrix. Neither arbitrary template instantiations nor invalid native C++ calls
are enumerated by this prototype.


IoC autowiring: records, enums, callbacks and expected failures
------------------------------------------------------------

Run the second experiment in the same environment::

    ~/miniconda3/envs/dnd/bin/python tests/prototypes/reflection_call_space/run_autowire.py

It selects the real ``pygim::core::plan_autowiring<int, const Lookup&>``
template specialization, included directly from
``src/_pygim_fast/wiring/ioc/core.h``. No production function, Python extension
or exported hook is changed.

``autowire_model.h`` supplies two dependency identities (0 and 1), two names
(first and second), a sequence bound of two, and a callable lookup environment.
The shared ``reflection_values.h`` discovers the public fields of opted-in
aggregate records and every named enumerator. It builds optional values and
bounded sequences recursively. Function parameter names and types still come
from the actual reflected declaration.

The shared model names its concrete types with ``using`` aliases: ``IntParam``,
``IntParams``, ``IntInjection``, ``IntPlan`` and ``LookupTrace``. The runtime and
compile-time helpers use those names to keep nested container types out of their
signatures; reflection still describes the underlying production types.

The finite model is:

* A parameter has two names, four reflected ``ParamKind`` values, three
  annotation states (absent, dependency 0, dependency 1), and two default states.
  This gives 48 parameter records.
* Sequences have lengths zero, one or two: 1 + 48 + 48 squared = 2,353 sequences.
  Order, repeated dependency identities, and repeated names are retained.
* The lookup has four provider sets (neither, 0, 1, both), crossed with three
  schedules (never fail, fail on the first lookup, fail on the second lookup).
  A scheduled failure only happens if execution reaches that lookup.
* The full product contains 2,353 times 12 = **28,236 native calls**.

A concrete call is represented as data, including the callback's environment::

    {
      "params": [
        {"name": "first", "kind": "KeywordOnly",
         "annotation": 0, "has_default": true},
        {"name": "second", "kind": "PositionalOrKeyword",
         "annotation": 1, "has_default": false}
      ],
      "contains": {"available": "Both", "fail_at": "Second"}
    }

The contract expects the first lookup to succeed and the second to raise the
injected lookup failure. A trace records ``[0, 1]``. It checks that an error
propagates, which error it is, its message, and precisely which lookups occurred.
An optional parameter still requires a lookup: its default does not suppress a
registered dependency or swallow a lookup failure. Parameter order controls
which validation error is reached first.

``verify_autowire.py`` is the human-authored contract. It independently computes
injections and expected errors from the declared rules, not from observed native
results. Unknown fields, enum meanings and callback states require contract
review. Exception classification and the lookup simulator are also explicit
test policies, not behavior recovered by reflection.

Measured results
~~~~~~~~~~~~~~~

The complete run on 2026-10-13 verified all 28,236 native calls:

* 7,052 successful injection plans.
* 14,016 expected validation errors.
* 2,240 expected missing-provider errors.
* 4,928 deliberately triggered callback exceptions.

All 14 outcomes at the policy's seven explicit loop/if sites were covered.
Raw gcov reports **15/16 condition outcomes and 28/42 branches**. The additional
condition is attributed to the function's closing brace (line 99 in the recorded
source); its missing outcome remains unresolved. The 14 unhit branch edges
include 11 marked ``throw`` and three others at that closing brace. Allocation
failure and exception-construction/cleanup behavior have not been fault-injected.
These remain visible coverage gaps, not exclusions or claims of unreachability.

The public adapter builds real Python constructors and calls
``pygim.ioc.Container.register(..., autowire=True)`` and ``resolve``. It verifies
872 successful calls and 1,700 expected errors: **2,572 public calls**. Both
``*args`` and ``**kwargs`` forms are attempted where the native Variadic kind
appears. The remaining audit rows are explicitly classified:

* 26,984 forms require a fault-injected lookup, which the public API does not
  expose. They are checked only in the native policy.
* 10,920 forms cannot be expressed as a valid Python signature, for example
  repeated names, a required positional parameter after an optional one, or a
  variadic parameter with a default.

These are counts of public *forms*, not distinct native rows. Some native rows
produce both variadic forms. Python/native parity is explicitly partial; the
adapter does not reinterpret an invalid signature to make it pass.

The generated pytest suite reruns the native binary and the public audit.
It includes an independent call-space check, four native outcome groups, a
public audit, and the behavior checks described below. Every native row is
asserted with its ID; batching does not sample or reduce the 28,236 calls.
A baseline run before behavior reporting had six tests passing in 1.68 seconds,
native execution plus JSON emission in
0.240633 seconds, native compilation in 2.758 seconds, and public audit in
0.641 seconds. The pytest subprocess took 2.110 seconds. These are observations,
not benchmark claims; this replay does not meet the one-second suite goal.

Checks that the checker works
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* Delete a call and adjust its reported count: the exact missing tuple is found.
* Delete a sequence from the advertised domain AND delete all its calls:
  reconstruction from the nested field model still finds the missing domain
  member. Agreement between two incomplete lists cannot pass this check.
* Add an unsupported ``double`` field to an opted-in record: compilation fails
  with the unmodeled-field diagnostic.
* In scratch copies of the real policy, skipping injection for parameters with
  defaults is caught by 6,368 calls; reversing the plan is caught by 256; changing
  missing-provider exceptions to generic runtime errors is caught by 2,240.
  Production code is never mutated.
* Add a fifth named ``ParamKind`` in a scratch copy: reflection automatically
  expands the model to 43,932 calls, and the contract rejects the new, undefined
  semantics. No handwritten list of parameter kinds was changed in the generator.

Evidence and boundaries
~~~~~~~~~~~~~~~~~~~~~~~

``build/reflection-call-space-autowire/latest.json`` points at the latest
successful run. Its directory contains the complete cases/model, native coverage,
public audit, generated replay plus snapshots of the verifiers and behavior rules, mutation
failures, enum-drift evidence, compiler logs, source hash, timings and a readable
``report.txt``. Each run starts with fresh compiler counters.

This experiment covers int dependency keys, declared callback adapters, and a finite input
model. It does not discover every possible template instantiation, arbitrary
callbacks, longer sequences, unnamed enum values, allocation schedules, or the
rest of the IoC container lifecycle. Record opt-ins, finite leaf domains,
callback effects, the contract and the public API mapping remain human inputs.
The callback trace is reset per call in a sequential standalone test process.



Behavior rules and overlap coverage
-----------------------------------

``autowire_behaviors.json`` adds a human-authored behavioral specification to
the same reflected call space. It contains 13 rules with stable IDs, descriptions,
Boolean input guards, and required actions. A rule can require continuation, a
lookup, omission, injection, or an exception with an exact type and message.
For example::

    {
      "id": "registered_beats_default",
      "description": "An available injection takes precedence over the parameter's default.",
      "stage": "provider",
      "when": {"annotated": true, "registered": true, "has_default": true},
      "requires": {"action": "inject"}
    }

The stage order is also data: ``kind → annotation → lookup → provider``,
repeated in parameter order. Earlier stages explicitly establish precedence.
Within one stage, every matching rule is a simultaneous obligation: changing
the order of rules cannot select a different winner. The example overlaps with
``inject_registered`` and both require injection. Requiring omission instead
would be a contradiction, not a new priority rule.

``behavior_autowire.py`` interprets that specification from argument data.
It compares the resulting ordered plan or exact exception and lookup trace with
both the existing independent ``expected_autowiring`` contract and the observed
native call. Neither native observations nor the interpreter's own result
supplies the reference oracle. Reflected schema drift still requires contract
review. The interpreter checks skipped parameters too, so an early failure
cannot conceal a contradictory specification at a later decision point.

Each rule occurrence has a parameter **index**, not just a name: repeated names
and repeated rules remain distinct. The report distinguishes:

* ``exercised``: the rule was reached by the specification and its call agrees
  with the reference contract and native observations.
* ``masked_by_failure``: an earlier throwing rule, on the same or an earlier
  parameter, prevented this otherwise matching rule. Its identity, stage and
  parameter index are retained as ``blocked_by``.
* ``masked_by_skip``: a rule such as ignoring a variadic parameter prevented a
  later check on that parameter. Later parameters still proceed.
* ``inferred_before_failure``: a non-lookup action precedes an eventual error.
  The function returns no partial plan, so that action receives no exercised
  credit. The lookup prefix and final exception remain observable.
* ``guard_unresolved``: a predicate needs a callback result, but no lookup was
  attempted. A scheduled second-lookup failure is not assumed to have happened
  at the second parameter. Unknown guards are not counted as established overlaps.
* ``contradictory``: simultaneous rules at the **same parameter and stage**
  require different values for an action, exception type or concrete message.
  These conflicts are reported even when an earlier failure masks that stage.
  Incompatible actions at different stages or parameter positions are not,
  by themselves, contradictions.

Specification gaps, undecidable guards at a reached stage, native mismatches,
reference-contract disagreements, and rules with no exercised witness fail the
report gate. Unverified calls never earn exercised credit. Rule predicates and
stage reachability remain **specified behavior**, not direct measurements of
native internal branches; the separate gcov report is still needed.

The overlap matrix reports pairs of matching rule occurrences, separately for
the same parameter and across parameters, including repeated instances of one
rule. Each category has an occurrence count, a distinct-case count, and up to
three witness IDs with both rule positions and blockers. One pair type can be
exercised in some calls and masked in others, so these categories must not be
summed as disjoint coverage percentages. Pairs without a joint match are labeled
only as such **within this finite model**, not universally impossible.
All applicable rules are checked together, but this report summarizes pairwise
overlaps; it does not claim to summarize every higher-order interaction.

The normal driver writes:

* ``behaviors/rules.json``: the exact rule specification used.
* ``behaviors/report.txt`` / ``report.json``: the readable report and full matrix.
* ``behaviors/cases.jsonl``: every case's expected, actual and reference outcome,
  rule occurrences, masking explanations, and specification diagnostics.
* ``behaviors/contradiction-control/``: a deliberately wrong additional rule,
  two real native witness cases, and diagnostics for both reached and masked
  conflicts. This scratch specification does not alter the real rules.
* A behavior diagnostic for a failing case from each existing native policy
  mutant, saved beside its ordinary contract failures.

The generated replay includes the behavior matrix check plus the focused
checker tests. It reruns native calls, recalculates behavior coverage from the
saved rules, and checks that the report remains reproducible. Focused checks
cover precedence, unknown predicates, actual lookup ordinals, repeated names,
unobservable partial plans, conflicting actions and messages, rule-list order,
missing rules, bad observations, and agreement with the independent contract.

To analyze an existing recording without compiling or running native code::

    ~/miniconda3/envs/dnd/bin/python tests/prototypes/reflection_call_space/behavior_autowire.py \
        build/reflection-call-space-autowire/<run>/cases.json \
        --output build/reflection-call-space-behaviors/<report-name>

Use ``--rules path/to/rules.json`` to inspect a changed specification. The report
is saved before a failing gate raises; a schema-invalid rule file is rejected
with its rule and offending field. This command analyzes recorded observations,
so it cannot verify changes to C++ made after that recording. The full driver
rebuilds and executes the real function and retains its existing static-assert
phase. Behavioral analysis itself does not require recompilation.

Run the checker tests alone with::

    ~/miniconda3/envs/dnd/bin/python -m pytest -q -c /dev/null \
        --rootdir tests/prototypes/reflection_call_space \
        tests/prototypes/reflection_call_space/check_autowire_behaviors.py

These remain prototype facilities, with no production policy, exported hook,
or installed extension changes.

Observed behavior run
~~~~~~~~~~~~~~~~~~~~~

Run ``run-1791907957982266149`` verified all **28,236** native cases and gave
every one of the **13 rules** an exercised witness. There were no specification
gaps, contradictions, reference disagreements, or native mismatches. Of the
169 rule-pair/relation types, 59 had exercised witnesses, 117 had failure-masked
witnesses, 82 had skip-masked witnesses, and 74 had inferred prefix witnesses;
37 had no joint match. These categories overlap, as described above.

For ``inject_registered + registered_beats_default`` on the same parameter:
1,408 cases contain an exercised occurrence; 4,744 contain a failure-masked
occurrence; 1,712 contain a skip-masked occurrence; 800 contain an inferred
prefix occurrence. ``default_does_not_swallow_failure + propagate_lookup_failure``
is exercised in 2,464 cases. Each count has concrete witness IDs in the report.

The scratch contradiction control adds ``defaults_suppress_injection``.
It conflicts with two existing injection obligations at ``case-130`` (reached)
and at ``case-202`` (masked by positional-only validation): four diagnosed
conflicts. Both native witness calls still have correct production behavior;
the deliberately broken object here is the specification, not the C++ code.

The complete replay passed **22 tests**: seven full-model groups and 15 focused
checker cases. The existing IoC tests passed 39 tests; Ruff passed. All 28,236
existing static assertions and their negative controls also passed. Observed
times were 3.205 seconds for behavior analysis with evidence serialization,
3.84 seconds inside pytest (4.210 seconds for its process), and 113.528 seconds
for successful static shard compilation. These are one-run observations, not
an optimization benchmark or a claim to meet the one-second suite goal. Raw
native coverage remains 15/16 condition outcomes and 28/42 branches.


Generated compile-time assertions
---------------------------------

The autowiring driver now also generates and compiles one ``static_assert`` for
every native row in that same 28,236-case model. Run the same command as above.
``--static-shard-size N`` changes how many assertions share a translation unit;
it does not change or sample the call space. The default is 1,024.

Generation remains a build step: the reflected JSON supplies argument data and
``verify_autowire.expected_autowiring`` supplies expected behavior. Observed
``native_result`` and ``native_error`` fields are never used as expected answers.
The generator emits constants for the existing independent contract, so there
is no second C++ implementation of the planner's decision rules to maintain.

For example, a generated proof has this form::

    static_assert(verify_autowire_case(
        {{"first", pygim::core::ParamKind::KeywordOnly, 0, true}},
        {Available::Both, FailAt::First},
        {},
        "lookup_failure", "injected lookup failure",
        {0}),
        "autowire case-...");

``autowire_static.h`` calls the real production ``constexpr`` planner and checks
the ordered result or exception type/message, plus the exact lookup trace.
Mutable state lives inside that constant evaluation. A local-trace callback
adapter delegates to the same ``Lookup::evaluate`` used by the runtime probe,
so the callback rules are shared. This instantiates the planner with int keys
and a LocalLookup callback, while the runtime probe uses int keys and Lookup.

The installed GCC 16 and libstdc++ evaluate all three error categories:
``std::runtime_error`` validation, the derived ``missing_key``, and the injected
``lookup_failure``. Constructors, throwing, catching, exception classification,
message comparison, vectors and strings all participate in constant evaluation.
The generated checks use ``-std=c++26 -freflection -fsyntax-only``. They need no
linked proof executable and do not run the planner at runtime to establish the
compile-time result. An unsupported compiler fails; there is no silent
conversion of unsuccessful compile-time cases into passing runtime cases.

The generated ``static/model_schema.h`` also checks the current function
signature, record fields, enum counts/values, scalar domains and sequence bounds
against the model used to generate the assertions. A saved suite therefore
rejects relevant schema changes instead of quietly continuing with its old
inputs. Regenerate after changing the source, contract or model.

The driver independently scans emitted assertion IDs and compares them with the
complete native model. Its negative controls remove an assertion, poison an
observed native result, compile a case against each of the three broken policy
copies, and compile an old schema against the added enum value. A policy
mutation must fail at a named ``autowire case-N`` assertion; an unrelated compiler
failure is not accepted as a detected behavioral bug.

Artifacts under the run's ``static/`` directory include:

* ``assertions_000.cpp`` and following shards: concrete, readable assertions.
* ``model_schema.h``: generated guards against schema drift.
* ``manifest.json``: every native case ID, expected outcome, source file and line.
* One compiler log per shard and per negative control.
* ``report.json`` / ``report.txt``: passing assertion counts, full compiler commands,
  compile durations and negative-control evidence.

The generated pytest suite still replays runtime behavior separately;
rerunning that pytest file does not recompile the static proofs. The full
``run_autowire.py`` command performs both phases. Compilation time is reported
separately and counts as real build/test cost; moving checks into the compiler
does not make the end-to-end process a one-second suite.

These are proofs over the declared finite cases. They do not establish arbitrary
input correctness, arbitrary callback behavior, actual allocation failures,
Python binding behavior, or 100% native branch coverage. Constant evaluation
does not exercise the runtime allocator or replace the runtime coverage report.


The complete run on 2026-10-13 compiled **28,236 / 28,236 case assertions**:
7,052 successful plans, 14,016 validation errors, 2,240 missing-provider errors,
and 4,928 injected lookup failures. No case was deferred to runtime. The 28
successful shard compilations took 121.015 seconds in total; generation,
verification and compile-failure controls brought the static phase to
128.686 seconds. These are one-run observations on the installed toolchain.

The broken-default policy failed at ``autowire case-49``, reversed injection
order at ``autowire case-1798``, and the wrong exception type at
``autowire case-37``. The runtime replay still passed all native/public checks
(six pytest groups in 1.84 seconds); the original CSV replay passed 780 tests,
the existing IoC tests passed 39 tests, and Ruff passed. Source changes remain
inside this opt-in prototype; no production policy or installed extension was
modified.

