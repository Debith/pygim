# Vendored: rapidyaml (ryml)

- **File:** `ryml_all.hpp` — the official single-header amalgamation.
- **Version:** v0.9.0
- **Source:** https://github.com/biojppm/rapidyaml/releases/download/v0.9.0/rapidyaml-0.9.0.hpp
- **License:** MIT (full text embedded at the top of `ryml_all.hpp`).

Do not edit `ryml_all.hpp` by hand — it is generated. To update, drop in a newer
release amalgamation and rebuild. `bindings.cpp` is the one translation unit that
defines `RYML_SINGLE_HDR_DEFINE_NOW`, so the implementation is emitted exactly once.
