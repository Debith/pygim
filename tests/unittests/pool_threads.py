"""What a child process reports of the threads a pygim run left behind. pygim names its threads
(utils/parallel.h): pygim-pool for a run's, which must be gone when the run ends, interrupted or
not; pygim-parked for the workers parallel::for_each keeps for the process. A count of every thread
read a parked worker started during the run as a leak, and another library's thread ending as one
fewer (PR #39's CI). Used by the Ctrl-C tests' child scripts."""

POOL_THREADS = r'''
import os
import time

def _pool_threads_now():
    alive = 0
    for tid in os.listdir("/proc/self/task") if os.path.isdir("/proc/self/task") else ():
        try:
            alive += open(f"/proc/self/task/{tid}/comm").read().strip() == "pygim-pool"
        except OSError:   # a thread that ended meanwhile
            pass
    return alive

def pool_threads():
    """How many of a run's pool threads are alive: none, once the run has ended (0 without /proc).
    Two moments read wrong, both brief: a joined thread stays in /proc until the kernel lets it go
    (join returns once its id is cleared, before that), and a thread just started bears its
    creator's name until its first instruction names it. Read until none is left, for at most a
    second: a thread a run truly left behind is still there then."""
    deadline = time.monotonic() + 1.0
    while True:
        alive = _pool_threads_now()
        if alive == 0 or time.monotonic() > deadline:
            return alive
        time.sleep(0.001)
'''
