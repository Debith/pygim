# Benchmark result series

One JSONL file per benchmark; each run of `python benchmarks/<bench>.py`
appends one record (pass `--no-save` to measure without recording).

A record carries the raw measurements (seconds, full precision) under
`sections`, plus the metadata that makes runs comparable over time:
`commit`, `dirty`, `branch`, `python`, `cpu`, `hostname`, `reps`.

When reading a series, **filter before trending**: compare only records with
the same `hostname`/`cpu` and `python`, and treat `dirty: true` records as
provisional (the commit hash does not describe the measured code).

Quick look at a series:

```bash
jq -r '[.utc, .commit, (.dirty|tostring), .python] | @tsv' \
    benchmarks/results/pathlike_decode.jsonl
```
