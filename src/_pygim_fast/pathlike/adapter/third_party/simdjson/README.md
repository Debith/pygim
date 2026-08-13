# Vendored: simdjson

- **Files:** `simdjson.h` / `simdjson.cpp` — the official amalgamation.
- **Version:** see `SIMDJSON_VERSION` in `simdjson.h`.
- **Source:** https://github.com/simdjson/simdjson/releases (latest/download assets).
- **License:** Apache-2.0 (full text embedded at the top of both files).

Do not edit these files by hand — they are generated. To update, drop in a newer
release amalgamation and rebuild. `simdjson.cpp` is compiled as its own source in
`ext.pathlike.toml`, so the implementation is emitted exactly once.
