# -*- coding: utf-8 -*-
"""Run Python code in a disposable child process with hard limits, for cases that may crash, hang or
exhaust memory: a new process group (killed whole on timeout), a wall-clock timeout, and limits on
address space and CPU time. The child sees this checkout's pygim (PYTHONPATH as the parent's) and no
user data: it runs in a fresh temporary directory, with no network use in any script given to it.

These budgets bound the tests; they are not limits pygim promises or imposes on its inputs.
"""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import tempfile
import textwrap
from dataclasses import dataclass

try:
    import resource
except ImportError:  # Windows: no rlimits; the wall clock still bounds the child
    resource = None


@dataclass(frozen=True)
class Outcome:
    returncode: int  # negative: killed by that signal
    stdout: str
    stderr: str
    timed_out: bool

    @property
    def crashed(self):
        return self.returncode < 0 and not self.timed_out


def _limits(memory_bytes, cpu_seconds):
    def apply():
        os.setsid()  # its own process group, so a timeout kills everything it started
        if resource is not None:
            resource.setrlimit(resource.RLIMIT_AS, (memory_bytes, memory_bytes))
            resource.setrlimit(resource.RLIMIT_CPU, (cpu_seconds, cpu_seconds))
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))  # a crash leaves no core file
    return apply


def run(source, *, timeout=20.0, memory_bytes=2 << 30, cpu_seconds=20):
    """Run `source` (dedented) in a fresh working directory with hard limits."""
    # Inherit the parent's full environment — Windows needs SYSTEMROOT and friends to start Python at
    # all, so a stripped whitelist leaves the child unable to launch (empty output). We only add our
    # own keys; isolation here is of the working directory and the resource limits, not the env.
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(p for p in sys.path if p and os.path.isdir(p))
    env["PYTHONFAULTHANDLER"] = "1"  # a native crash prints the Python stack it happened under
    with tempfile.TemporaryDirectory() as workdir:
        process = subprocess.Popen([sys.executable, "-c", textwrap.dedent(source)], cwd=workdir, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                   preexec_fn=_limits(memory_bytes, cpu_seconds) if os.name == "posix" else None)
        try:
            stdout, stderr = process.communicate(timeout=timeout)
            return Outcome(process.returncode, stdout, stderr, False)
        except subprocess.TimeoutExpired:
            if os.name == "posix":
                os.killpg(process.pid, signal.SIGKILL)
            else:
                process.kill()
            stdout, stderr = process.communicate()
            return Outcome(process.returncode, stdout, stderr, True)
