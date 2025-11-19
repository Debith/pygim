Changelog
=========

.. NOTE FOR CONTRIBUTORS --------------------------------------------------
   - Keep newest entries at the top.
   - Group changes under: Added / Changed / Fixed / Removed / Performance / Docs.
   - Use past tense or imperative consistently (here: imperative mood, e.g. "Add", "Fix").
   - Reference PR numbers in parentheses when available, e.g. (PR #4).
   - Do not list trivial internal refactors unless they affect public API, performance, or developer workflow.
   - For unreleased work, accumulate under "Unreleased"; move to a versioned heading when bumping __version__.

Unreleased
----------
Added
~~~~~
- Initial CHANGELOG with retroactive notes for registry enhancement work.
- Registry: Decorator-based registration via ``@registry.register(key, override=False)``.
- Registry: ``registered_keys()`` introspection method for current logical ids.
- Registry: ``find_id(obj)`` fast reverse lookup (qualname policy only).
- Registry: Optional ``capacity`` constructor arg for upfront map reservation.
- Registry: Explicit ``post(key, value)`` trigger to manually invoke post hooks.
- Examples: Two runnable registry examples under ``docs/examples/registry/`` (basic & hooks).
- CI: Add release workflow that builds wheels via ``cibuildwheel`` and publishes tagged releases to PyPI.
- CI: Auto-tag main whenever the ``dev`` branch is merged, driven by PR labels ``release:major``/``release:minor``/default patch.

Changed
~~~~~~~
- Registry: Override path optimized to single unordered_map probe (eliminated double lookup).
- Registry: ``__repr__`` now includes ``policy``, ``hooks`` flag, and current ``size``.
- Docs: Expanded ``_pygim_fast/registry.h`` with architectural overview & guidance.
- Project instructions updated to reflect new registry surface area (PR #4).

Fixed
~~~~~
- Tests: Ensured override semantics correctly raise when ``override=True`` and key is missing.

Performance
~~~~~~~~~~~
- Reduced overhead on override operations through consolidated probe.

Docs
~~~~
- Added inline binding docstrings for new registry APIs.
- Added educational examples demonstrating hooks and override semantics.

0.0.1 (Initial)
---------------
- Project scaffolding.
- Basic extension modules layout.
- Initial registry & supporting infrastructure (pre-enhancement state).
