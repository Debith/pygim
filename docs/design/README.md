# Design Diagrams Index

This folder contains architecture diagrams split by **current implementation** and **target state**.

## Registry

### Current
- `registry_current_class_diagram.puml`
- `registry_current_sequence_diagram.puml`

### Target
- `registry_target_class_diagram.puml`
- `registry_target_sequence_diagram.puml`

## Factory

### Current
- `factory_current_class_diagram.puml`
- `factory_current_sequence_diagram.puml`

### Target
- `factory_target_class_diagram.puml`
- `factory_target_sequence_diagram.puml`

## Repository

### Abstract
- `repository_architecture_abstract.puml` — class diagram: interfaces only (Repository, Strategy, QueryDialect, value types)
- `repository_sequence_abstract.puml` — sequence diagram: persist_dataframe, fetch_raw, save flows at interface level

### Current
- `repository_architecture_current.puml` — full class diagram with implementations

### Sequences
- `repository-seq-diagram.puml` — persist_dataframe call flow
- `arrow_bcp_sequence.md` — detailed BCP row-loop sequence
- `repository_acquire_sequence.puml` — fetch/acquire sequence

### Performance
- `repository_performance_findings.md` — measured throughput baselines, bottleneck analysis, and remediation priorities for MSSQL Arrow/BCP write paths.

### Backlog
- `repository_backlog.md` — phased implementation plan: architecture consolidation → tiled transpose → SIMD → multithreading → pipeline + connection pool → caching.

## Notes
- **Current** diagrams are 1-to-1 reflections of existing headers/flow.
- **Target** diagrams describe the pybind-isolated architecture with compile-time constraints.
- Keep this index as the single navigation entry-point to avoid duplicated diagram summaries elsewhere.
