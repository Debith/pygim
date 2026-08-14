# Benchmark result series

One JSONL file per benchmark, **one record per merge to main**: run
`python benchmarks/<bench>.py` on the branch about to merge and let the
appended record ride in with the merge (pass `--no-save` to measure without
recording). The record's `commit` field is null by convention — a file
cannot contain the hash of the commit it is part of; its location in
history IS the attribution, and `git log` on this file dates every point.

A record carries the raw measurements (seconds, full precision) under
`sections`, plus the metadata that makes runs comparable over time:
`dirty`, `branch`, `python`, `cpu`, `hostname`, `reps`.

When reading a series, **filter before trending**: compare only records with
the same `hostname`/`cpu` and `python`.

Quick look at a series:

```bash
jq -r '[.utc, .commit, (.dirty|tostring), .python] | @tsv' \
    benchmarks/results/pathlike_decode.jsonl
```
