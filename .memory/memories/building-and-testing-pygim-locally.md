---
memory: 3e84752dd6ff3cfb14cd27b093c95eb1
title: "Building and testing pygim locally"
origin: written
tags: ["domain=pygim","component=build","artifact=extension","task=implement","kind=procedure","concern=toolchain","language=python"]
---
1. Work inside the conda environment `dnd` (~/miniconda3/envs/dnd: Python 3.12, pyarrow 23, conda's gcc_linux-64/gxx_linux-64, unixODBC from conda-forge). There is no system g++; the compiler comes from the environment's activation scripts, and setup.py reads CONDA_PREFIX for headers, libraries and the RPATH — so extensions must be built from inside the environment they will run in.
2. A shell that has not activated it (an agent's shell, CI steps) runs everything as `~/miniconda3/bin/conda run -n dnd --no-capture-output <cmd>`. In such a shell `pip`, `pipx`, `uv` and `python3 -m pip` may be missing and `python3` may be the system one; use the environment's python.
3. Rebuild with `python -m pip install -e . --no-build-isolation`. With build isolation, pip installs the newest pyarrow into a throwaway build environment, `_persistence.so` links that libarrow, the environment vanishes, and persistence imports fail at runtime.
4. Test with `python -m pytest tests/unittests`. Skips for Python 3.13 features and a live MSSQL driver are expected locally.
5. Extensions are mandatory: setup.py stops on a missing dependency rather than skipping it. Install system libraries with `conda install -n dnd --override-channels -c conda-forge <pkg>` (the defaults channel refuses on its terms of service).
