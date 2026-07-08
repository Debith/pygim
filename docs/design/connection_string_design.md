# Connection-string subsystem — design

Status: **Phase 1 implemented (2026-07-08)** · Audience: `pygim` maintainers · Diagram: [`connection_string_design.puml`](connection_string_design.puml)

> **Phase 1 landed:** `core/connection_string.h` (value object + builder +
> `OdbcDsnFactory`/`SqlAlchemyUrlFactory` + `parse()` facade + constexpr
> `detail` helpers) is implemented, bound to Python as `ConnectionString`
> (`str`/`repr`/`render()` mask the password), and wired into
> `RepositoryAdapter::create` — which now parses the source (URL **or** DSN) in
> C++ and derives the token-conflict check from the parsed keys. The Python
> `_translate_conn_str`/`_dsn_quote` translation was removed; `_translate_conn_str`
> remains as a thin shim over `ConnectionString`. **Phase 2 (not yet done):**
> migrate the `std::string m_conn_str` members in `ConnectionPool` /
> `OdbcConnection` / `MssqlLoadCache` to hold `ConnectionString`, and move
> `pack_access_token` / `ensure_packet_size` into core.

## Problem

Connection-string knowledge is scattered across three languages/layers with no
type that owns it (verified inventory, 2026-07-08):

| Concern | Lives today | Layer |
| --- | --- | --- |
| URL → ODBC DSN translation, brace-quoting | `persistence.py` `_translate_conn_str` / `_dsn_quote` | Python |
| `PacketSize` append | `strategy/mssql/backend.h` `ensure_packet_size` | mssql |
| UID/PWD/Trusted/Authentication conflict scan | `adapter/adapter.h` (lowercased substring test) | adapter |
| `SQL_COPT_SS_ACCESS_TOKEN` byte-packing | `adapter/adapter.h` `pack_access_token` | adapter |
| The string itself | 4× independent `std::string m_conn_str` copies (pool, `OdbcConnection`, `MssqlLoadCache`, transient worker copies) | core / mssql |

`core::Query` models a **SQL query**, not a connection string — there is no
existing connection-string type. The substring conflict scan is also a latent
bug: `cs_lower.contains("pwd=")` false-positives on a value like
`Driver={pwd=weird}`.

## Goal

One **immutable value object** in the pybind-free `core/` layer that owns
parsing, building, quoting, the packet-size/token-conflict policy, and rendering
back to a raw DSN. Everything downstream stores that value instead of a bare
string. Per the maintainer decision (2026-07-08): the SQLAlchemy-URL translation
**moves from Python into C++ core**, so the Python `acquire_datastore` wrapper
becomes a pass-through.

## Shape

Three collaborating roles (see diagram):

- **`ConnectionString`** — the immutable value ("the const expression"). No
  public constructor; you get one only from the builder or `parse()`. All
  queries are `const`; "modifications" (`with_packet_size`, `with`) return a new
  value, so it behaves like a `const` throughout the pool/connection lifetime.
  It has **value equality** (`operator==`, normalized keys) so `MssqlLoadCache`
  keys on the value rather than comparing rendered strings.
- **`ConnectionStringBuilder`** — fluent, mutable assembly of parts →
  `build()` produces a `ConnectionString`, brace-quoting each value via
  `dsn_quote()`.
- **`ConnectionStringFactory`** (abstract factory) — one concrete product per
  source dialect: `OdbcDsnFactory` (raw `key=value;`) and `SqlAlchemyUrlFactory`
  (`mssql+pyodbc://…`, absorbing `_translate_conn_str`). A
  `ConnStringFactorySelector` sniffs the source (`://` + scheme) and returns the
  right one.

### How builder and factory collaborate

Both entry points exist and compose the way you asked — the builder uses the
factory when it is handed a whole source string, and the factories use the
builder to assemble their product:

```text
ConnectionString::parse(source)            // facade
  └─ ConnStringFactorySelector::select(source)   // abstract factory dispatch
       └─ SqlAlchemyUrlFactory::create(source)    // or OdbcDsnFactory
            └─ ConnectionStringBuilder{...}.build()  // factory drives builder

// piece-by-piece, no source string:
ConnectionStringBuilder{}.driver(d).server(h, port).database(db).build()

// builder handed a whole source → delegates to the factory:
ConnectionStringBuilder::from_source(source)   // == ConnectionString::parse
```

So: **factory → builder** to assemble; **builder/value-object → factory** when
the input is an opaque source rather than parts. No cycle at runtime — the
static `from_source`/`parse` facades are the only edge back to the selector.

## constexpr story (honest scope)

Full end-to-end `constexpr` is not achievable (a rendered DSN needs heap
`std::string`, and `constexpr std::string` can't persist a transient allocation
to runtime). What **is** `constexpr`, and worth unit-testing with `static_assert`
exactly like `core::plan_autowiring`:

- `ConnKey::canonical()` / `spelling()` — keyword classification
- `dsn_quote()`, `sniff_source()`, `to_lower_ascii()` — the pure helpers
- `ConnectionString::has()`, `is_named_dsn()`, `token_conflicts()` — the policy
  predicates over already-parsed attributes
- `AccessTokenPacker::pack()` — length-prefix + UTF-16LE expansion

The value object is therefore **immutable and constexpr-evaluable in its pure
surface**, while the string-producing `render()`/`create()` run at runtime.

## Migration (what each scattered site becomes)

| Today | Becomes |
| --- | --- |
| `persistence.py` `_translate_conn_str` / `_dsn_quote` | `SqlAlchemyUrlFactory` + `dsn_quote()`; Python wrapper passes the raw source through |
| `backend.h` `ensure_packet_size` | `ConnectionString::with_packet_size(n)` |
| `adapter.h` UID/PWD/Trusted/Auth substring scan | `ConnectionString::token_conflicts()` (real key check) |
| `adapter.h` `pack_access_token` byte-packing | `core::AccessTokenPacker::pack()` (adapter keeps only `py::object` dispatch + `py::value_error` translation) |
| `m_conn_str` in `ConnectionPool`, `OdbcConnection`, `MssqlLoadCache` (+ worker copies) | one `ConnectionString` member; `.render(WithSecrets)` at the `SQLDriverConnect` boundary |
| `MssqlBackend::connect(std::string_view, int)` | `connect(const ConnectionString&, int)` |

### Layer discipline preserved

- **core/** owns parse/build/quote/classify/pack — pure, no pybind, no ODBC
  headers.
- **adapter/** keeps only `py::object` type-dispatch and error translation.
- **strategy/mssql/** keeps the `SQLSetConnectAttr` / `SQLDriverConnect` calls;
  it now receives a `ConnectionString` and calls `.render()` at the last moment.

## Rendering: masked by default

`render()` has two formats, and the **safe one is the default** so a credential
never leaks by accident into a log line, an `OdbcError.what()`, or a `repr`:

- `render(Reveal::Masked)` *(default)* — `Pwd` and any access token become
  `***`; everything else verbatim. This is what diagnostics, logging, and the
  `__repr__` use.
- `render(Reveal::WithSecrets)` — the full DSN, used **only** at the
  `SQLDriverConnect` boundary in `OdbcConnection::open`.

Rule of thumb for consumers: if the string is going anywhere except the driver,
call the default. `mask_secret()` (a `core::detail` helper) does the redaction so
the mask policy is defined in exactly one place.

## Resolved decisions (2026-07-08)

1. **Namespace** — public types (`ConnectionString`, `ConnectionStringBuilder`,
   `ConnectionStringFactory`) sit flat in `pygim::core`, matching the repo's
   "sub-namespace = layer, not subsystem" convention (`Query`/`ConnectionPool`
   are flat too). Pure helpers and the hidden-friend `operator==` live in
   `pygim::core::detail` so they don't pollute `core`'s overload set. Promotion
   to `core::conn` later is a mechanical refactor if the team formalizes
   subsystem grouping.
2. **Equality/caching** — `ConnectionString` has value equality (`operator==`
   over normalized keys); `MssqlLoadCache` keys on the value, not on `render()`.
3. **Phasing** — land the value object + two factories first (behavior-
   preserving, covered by the existing `test_sqlalchemy_url_*` tests), then
   migrate token-packing in a second pass to keep diffs reviewable.

## Still open

- **Passthrough key case**: preserve the caller's verbatim spelling for `Other`
  keys (current plan) vs canonicalize everything.
