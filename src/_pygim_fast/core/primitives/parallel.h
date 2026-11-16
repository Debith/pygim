#pragma once
// core/primitives/parallel.h — the thread pools, one header for all of them: run n indexed tasks on as many threads as they reward, handing the
// results to one consumer in index order, with at most a window of them waiting.
//
//     ordered_pool pool(n, [&](std::size_t i) { return work(in[i]); }, elastic_width(cores, elastic_cap(cores)),
//                       window_limits(1'024, 64 << 20));
//     while (!pool.exhausted()) use(pool.next());          // result 0, 1, 2, ... — each task run once
//
// The consumer is a worker too: when nobody has started the task it needs next, it runs it itself,
// so one thread costs no hand-off. Each finished task may start more workers, as many as the width
// policy says the rest is worth: elastic_width while one more thread saves more than the ~20 µs it
// costs to start, up to the cores for tasks that compute and up to a cap for tasks that wait (the
// share measured by sampling each thread's CPU clock); fixed_width(n) n threads from the start. Workers claim tasks in index
// order and wait when they are a window ahead of the consumer — `results` results, or `bytes` of
// them as Weigh weighs them — so memory stays bounded however many tasks there are; the task the
// consumer needs next is always admitted.
//
// Why this shape, measured 2026-10-25 on 1,759 files of CPython's Lib (32 MB; 16 logical cores; warm
// cache, cold cache, the 9p mount /mnt/c): a worker that reads and then processes beat separate
// reader and processing stages everywhere (their hand-offs cost up to 2x); elastic growth came
// within 10-20% of the best fixed width on every storage without knowing which it was (a probe of
// the first 32 files cost 90 ms on the 9p mount; feedback on throughput converged too slowly for
// jobs this short); waiting reads want threads well past the cores (cold: 48 threads beat 16, on 4
// cores too), hence the cap of at least 32; and a window of 1,024 instead of 64 halved CPU-heavy
// jobs, because in-order delivery otherwise stalls every worker behind one large task.
//
// Waiting is on 32-bit atomics, one per slot: libstdc++ on Linux sleeps on a 32-bit atomic's own
// address (a futex), while a 64-bit one parks its waiters on a small shared table, spins first and
// wakes all that hash alike — sixteen idle workers then burnt 390 ms of CPU and slowed the consumer
// 1.8x (#269). That is libstdc++'s on Linux: a library whose native wait is another width (libc++
// off Linux) takes every 32-bit wait through its table, where the herd remains.
//
// CORE layer: pybind-free. A task may touch only what belongs to its index; next() is called from
// one thread at a time.
//
// Each part is a header of its own in parallel/: width.h, window.h, meter.h, crew.h, for_each.h,
// ordered_pool.h and task_pool.h.

#include "parallel/width.h"
#include "parallel/window.h"
#include "parallel/meter.h"
#include "parallel/crew.h"
#include "parallel/for_each.h"
#include "parallel/ordered_pool.h"
#include "parallel/task_pool.h"
