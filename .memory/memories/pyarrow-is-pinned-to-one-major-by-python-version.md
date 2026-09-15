---
memory: e2ba3f047b6253e7d6bd4960e77168f1
title: "pyarrow is pinned to one major, by Python version"
origin: written
tags: ["domain=pygim","component=build","artifact=extension","task=design","kind=decision","concern=toolchain","concern=portability","language=config"]
---
Release wheels leave libarrow and libparquet out of auditwheel/delocate repair and link the user's pyarrow through an `$ORIGIN/../pyarrow` rpath, so a wheel needs the soname of the pyarrow major it was built against: build-time and runtime majors must match. Pin pyarrow to one major in both [build-system].requires and [project].dependencies (decided 2026-07-08).

pyarrow 22 and later need Python 3.10, while pygim still supports 3.9, so a flat `pyarrow>=23,<24` fails the 3.9 CI job at install. The pin is therefore conditional: `pyarrow>=23,<24; python_version>='3.10'` and `pyarrow>=21,<22; python_version<'3.10'` (21 is the last major that supports 3.9). If 3.9 is dropped — it has been end-of-life since October 2025 — collapse these to one pin.
