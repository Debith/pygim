#pragma once
// utils/parallel.h — run n indexed tasks on as many threads as they reward, handing the results to
// one consumer in index order, with at most a window of them waiting.
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

#include <algorithm>
#include <atomic>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#include <signal.h>
#endif

#include "duration.h"
#include "timestamp.h"

namespace pygim::parallel {

// ── how wide ─────────────────────────────────────────────────────────────

/// What the tasks done so far cost: how many threads the rest of them is worth.
class item_cost {
public:
    /// What one more thread costs to start and join: 8 threads over 8 files took 179 µs where one
    /// took 33 (Linux, 2026-10-25).
    static constexpr duration thread_start = std::chrono::microseconds(20);

    constexpr item_cost(duration spent, std::size_t items) noexcept : m_spent(spent), m_items(items) {}

    /// The threads for `remaining` more tasks at this cost, between one and `cap`: w threads take
    /// about (w - 1)·thread_start + work / w, so widen while the next thread saves more than it
    /// costs, work / (w·(w + 1)) > thread_start.
    [[nodiscard]] constexpr std::size_t width_for(std::size_t remaining, std::size_t cap) const noexcept {
        const duration work = work_of(remaining);
        std::size_t w = 1;
        while (w < cap && thread_start * static_cast<duration::rep>(w * (w + 1)) < work) ++w;
        return w;
    }

private:
    [[nodiscard]] constexpr duration work_of(std::size_t remaining) const noexcept {
        if (m_items == 0) return duration(0);
        return m_spent / static_cast<duration::rep>(m_items) * static_cast<duration::rep>(remaining);
    }

    duration m_spent;
    std::size_t m_items;
};
static_assert(item_cost(std::chrono::microseconds(32), 32).width_for(30, 16) == 1);     // 30 µs of work: not worth a thread
static_assert(item_cost(std::chrono::microseconds(320), 32).width_for(32, 16) == 4);    // 320 µs: four
static_assert(item_cost(std::chrono::microseconds(320), 32).width_for(1'000, 16) == 16); // 10 ms: the cap
static_assert(item_cost(duration(0), 0).width_for(1'000, 16) == 1);

/// How much of a task's time its thread spent on a CPU rather than waiting — on a disk, a network.
/// Threads past the cores pay only for the waiting: a warm read copies pages and slows down past
/// the cores (16 threads 2.2 ms, 32 threads 8.4), a cold one waits and keeps gaining (16: 27.8 ms,
/// 32: 17.0; 2026-10-25).
class compute_share {
public:
    constexpr compute_share(duration on_cpu, duration wall) noexcept : m_on_cpu(on_cpu), m_wall(wall) {}

    /// The most threads worth running: the cores when tasks only compute, more as they wait —
    /// cores x wall / on-CPU time — but never more than `cap`, nor fewer than the cores.
    [[nodiscard]] constexpr std::size_t threads_worth(std::size_t cores, std::size_t cap) const noexcept {
        const std::size_t floor = std::min(cores, cap);
        if (m_on_cpu <= duration(0)) return floor;   // unmeasured
        const auto scaled = static_cast<std::size_t>(m_wall.count() / std::max<duration::rep>(m_on_cpu.count() / static_cast<duration::rep>(cores), 1));
        return std::clamp(scaled, floor, cap);
    }

private:
    duration m_on_cpu;
    duration m_wall;
};
static_assert(compute_share(std::chrono::milliseconds(10), std::chrono::milliseconds(10)).threads_worth(16, 32) == 16);   // computes: the cores
static_assert(compute_share(std::chrono::milliseconds(1), std::chrono::milliseconds(10)).threads_worth(16, 32) == 32);    // waits 90%: the cap
static_assert(compute_share(std::chrono::milliseconds(8), std::chrono::milliseconds(10)).threads_worth(16, 32) == 20);    // waits 20%: 16 / 0.8
static_assert(compute_share(duration(0), duration(0)).threads_worth(16, 32) == 16);

/// How many threads a pool runs, the consumer counted: before any task is measured, and after.
template <class W>
concept width_policy = requires(const W& width, const item_cost& cost, const compute_share& share, std::size_t remaining) {
    { width.at_start() } -> std::same_as<std::size_t>;
    { width.for_the_rest(cost, share, remaining) } -> std::same_as<std::size_t>;
};

/// `threads` threads from the start (at least one).
class fixed_width {
public:
    constexpr explicit fixed_width(std::size_t threads) noexcept : m_threads(std::max<std::size_t>(threads, 1)) {}

    [[nodiscard]] constexpr std::size_t at_start() const noexcept { return m_threads; }
    [[nodiscard]] constexpr std::size_t for_the_rest(const item_cost&, const compute_share&, std::size_t) const noexcept { return m_threads; }
    [[nodiscard]] constexpr bool operator==(const fixed_width&) const noexcept = default;

private:
    std::size_t m_threads;
};
static_assert(width_policy<fixed_width> && fixed_width(0) == fixed_width(1) && fixed_width(4).at_start() == 4);

/// The consumer alone at first; then as many threads as the rest is worth: the cores for work that
/// computes, more for work that waits, up to `cap`.
class elastic_width {
public:
    constexpr elastic_width(std::size_t cores, std::size_t cap) noexcept
        : m_cores(std::max<std::size_t>(cores, 1)), m_cap(std::max<std::size_t>(cap, 1)) {}

    [[nodiscard]] constexpr std::size_t at_start() const noexcept { return 1; }
    [[nodiscard]] constexpr std::size_t for_the_rest(const item_cost& cost, const compute_share& share, std::size_t remaining) const noexcept {
        return cost.width_for(remaining, share.threads_worth(m_cores, m_cap));
    }

private:
    std::size_t m_cores;
    std::size_t m_cap;
};
static_assert([] {
    const item_cost ten_us_each(std::chrono::microseconds(320), 32);
    const compute_share computes(std::chrono::milliseconds(1), std::chrono::milliseconds(1));
    const compute_share waits(std::chrono::milliseconds(1), std::chrono::milliseconds(10));
    const elastic_width width(16, 32);
    return width_policy<elastic_width> && width.at_start() == 1 && width.for_the_rest(ten_us_each, computes, 1'000) == 16 &&
           width.for_the_rest(ten_us_each, waits, 1'000) == 22 && width.for_the_rest(ten_us_each, waits, 10) == 2;
}());

/// The cap an elastic pool is wired with: threads that wait on a disk pay well past the cores (cold:
/// 48 threads beat 16, on 4 cores too), so at least 32, and twice the cores where there are more.
[[nodiscard]] constexpr std::size_t elastic_cap(std::size_t cores) noexcept { return std::max<std::size_t>(32, 2 * cores); }
static_assert(elastic_cap(4) == 32 && elastic_cap(64) == 128);

/// The cores this machine offers; one when it cannot tell. Machine state: read where a pool is wired.
[[nodiscard]] inline std::size_t this_machines_cores() noexcept {
    return std::max<std::size_t>(std::thread::hardware_concurrency(), 1);
}

/// The most threads one for_each starts.
inline constexpr std::size_t max_workers = 64;

/// The worker count for `items` independent pieces of work: every hardware
/// thread (at most max_workers), never more than there are pieces, at least one.
[[nodiscard]] inline std::size_t workers_for(std::size_t items) {
    return std::max<std::size_t>(1, std::min<std::size_t>({this_machines_cores(), items, max_workers}));
}

// ── the threads the pools start ─────────────────────────────────────────

/// Names the calling thread, as `top -H`, a debugger and /proc/<pid>/task/<tid>/comm show it (at
/// most 15 characters on Linux): pygim-pool for a run's threads, joined when it ends, pygim-parked
/// for the workers parallel::for_each keeps. Where threads have no names (Windows here), nothing.
inline void name_this_thread(const char* name) noexcept {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), name);
#elif defined(__APPLE__)
    pthread_setname_np(name);
#else
    (void)name;
#endif
}

/// While it lives, the calling thread blocks every asynchronous signal, so the threads it starts
/// inherit the mask (a thread starts with its creator's). A process's signal goes to a thread that
/// does not block it: Ctrl-C then reaches the thread that can act on it — Python's main thread,
/// whose blocking call it cuts short — never a pool's worker, which would take it and leave that
/// call running (Linux offers it to the main thread first; macOS to any). A fault (SIGSEGV, SIGBUS,
/// SIGFPE, SIGILL) stays the faulting thread's. Where there are no POSIX signals, nothing.
class signals_blocked {
public:
    signals_blocked() noexcept {
#if defined(__unix__) || defined(__APPLE__)
        sigset_t all;
        sigfillset(&all);   // unqualified: macOS defines sigfillset and sigdelset as macros
        for (int fault : {SIGSEGV, SIGBUS, SIGFPE, SIGILL}) sigdelset(&all, fault);
        m_restore = ::pthread_sigmask(SIG_BLOCK, &all, &m_before) == 0;
#endif
    }
    ~signals_blocked() {
#if defined(__unix__) || defined(__APPLE__)
        if (m_restore) ::pthread_sigmask(SIG_SETMASK, &m_before, nullptr);
#endif
    }
    signals_blocked(const signals_blocked&) = delete;
    signals_blocked& operator=(const signals_blocked&) = delete;

private:
#if defined(__unix__) || defined(__APPLE__)
    sigset_t m_before{};
    bool m_restore = false;
#endif
};

// ── the threads for_each borrows ─────────────────────────────────────────

/// One parked_workers job as a 64-bit word: how many helpers it wants (high half) and how many
/// tickets have been taken (low). A ticket below the helpers wanted is a real one — a helper's
/// share of the job — and any later one surplus, with which a thread touches nothing.
class job_word {
public:
    /// A new job for `helpers` helpers, no ticket taken.
    [[nodiscard]] static constexpr std::uint64_t opened(std::size_t helpers) noexcept { return std::uint64_t{helpers} << 32; }

    constexpr explicit job_word(std::uint64_t word) noexcept : m_word(word) {}

    [[nodiscard]] constexpr std::uint32_t helpers() const noexcept { return static_cast<std::uint32_t>(m_word >> 32); }
    [[nodiscard]] constexpr std::uint32_t tickets_taken() const noexcept { return static_cast<std::uint32_t>(m_word & 0xffff'ffffu); }
    /// Whether the ticket this word's taker drew is a real one.
    [[nodiscard]] constexpr bool real_ticket() const noexcept { return tickets_taken() < helpers(); }
    /// How many real tickets were taken when the job closed at this word.
    [[nodiscard]] constexpr std::uint32_t helpers_working() const noexcept { return std::min(tickets_taken(), helpers()); }

private:
    std::uint64_t m_word;
};
static_assert(job_word(job_word::opened(3) + 2).real_ticket() && !job_word(job_word::opened(3) + 3).real_ticket() &&
              job_word(job_word::opened(3) + 5).helpers_working() == 3 && job_word(job_word::opened(3) + 1).helpers_working() == 1);

/// The process's threads for for_each: started at the first for_each that wants more than one,
/// one fewer than the cores (the caller works too), parked between calls. A call that finds them
/// lent — a for_each inside a task, or one on another thread at the same time — or that wants
/// more threads than the pool holds starts its own, as every call did before. Starting and
/// joining 16 threads cost each for_each 500 µs, and a token index build makes eight
/// (2026-10-28, Linux).
///
/// A job is one 64-bit word (job_word). The caller publishes the job, wakes as many parked
/// threads, runs tasks itself, and when the tasks are gone adds `helpers` tickets so every later
/// one is surplus; it then waits only for the helpers that took a real ticket. A thread that wakes
/// late takes a ticket of whatever job is current — a real one, or a surplus one with which it
/// touches nothing — so a job's body is read only by helpers its caller waits for. After a fork
/// the child starts a pool of its own. Each extension module that includes this header has its
/// own pool (its own copy of these statics).
///
/// The threads park for the life of the process: once a pool has run, the process has threads,
/// and a later fork() is a fork of a process with threads (Python 3.12+ warns of it: what another
/// thread held in the parent is held for ever in the child, which only these statics' own
/// atfork handler takes care of). They block every asynchronous signal (signals_blocked).
class parked_workers {
public:
    /// The process's pool; created on first use, then reached without a lock.
    [[nodiscard]] static parked_workers& process() {
        if (parked_workers* pool = current().load(std::memory_order_acquire)) return *pool;
        std::lock_guard lock(creating());
        if (current().load(std::memory_order_relaxed) == nullptr) {
            current().store(new parked_workers(this_machines_cores() - 1), std::memory_order_release);   // never destroyed: its threads park until the process ends
        }
        return *current().load(std::memory_order_relaxed);
    }

    /// Runs body(0) on the calling thread and body(1..helpers) on parked threads as they wake, if
    /// the pool is free and holds `helpers` threads; returns false, running nothing, otherwise.
    /// `body` returns once no task is left, wherever it runs. Whatever body(0) throws is rethrown
    /// once every helper that took a share has left the body; what a helper's body throws, after.
    template <class Body>
    bool run(std::size_t helpers, Body& body) {
        if (helpers > m_threads.size() || m_busy.exchange(true, std::memory_order_acquire)) return false;
        class lent {
        public:
            explicit lent(std::atomic<bool>& busy) noexcept : m_busy(busy) {}
            ~lent() { m_busy.store(false, std::memory_order_release); }

        private:
            std::atomic<bool>& m_busy;
        } returned_after(m_busy);
        m_body = &body;
        m_call = [](void* b, std::size_t worker) { (*static_cast<Body*>(b))(worker); };
        m_done.store(0, std::memory_order_relaxed);
        m_job.store(job_word::opened(helpers), std::memory_order_release);   // no ticket taken yet
        m_wake.fetch_add(1, std::memory_order_release);
        if (2 * helpers >= m_threads.size()) m_wake.notify_all();   // one wake for many: each notify is a syscall on the caller's path
        else for (std::size_t k = 0; k < helpers; ++k) m_wake.notify_one();
        std::exception_ptr own;
        try {
            body(0);
        } catch (...) {
            own = std::current_exception();   // rethrown below: the helpers read `body` until they are waited for
        }
        const job_word closed(m_job.fetch_add(helpers, std::memory_order_acq_rel));   // the tickets left are no one's now
        for (std::uint32_t done; (done = m_done.load(std::memory_order_acquire)) != closed.helpers_working();) m_done.wait(done, std::memory_order_acquire);
        if (own) std::rethrow_exception(own);
        if (m_error) std::rethrow_exception(std::exchange(m_error, nullptr));   // what a helper's body let out (for_each's catches all)
        return true;
    }

private:
    // Starts up to `threads` parked threads: as many as the system lets start, none when it lets
    // none (a pool of none lends nothing, and every for_each runs on threads of its own or alone).
    explicit parked_workers(std::size_t threads) {
        m_threads.reserve(threads);
        const signals_blocked quiet;   // inherited: the parked threads never take a signal
        try {
            for (std::size_t t = 0; t < threads; ++t) m_threads.emplace_back([this] { name_this_thread("pygim-parked"); serve(); });
        } catch (const std::system_error&) {
        }
#if defined(__unix__) || defined(__APPLE__)
        // a fork while another thread creates the pool would leave the child's lock held: it is
        // held across the fork, and the child starts from no pool
        static const bool reset_in_child = [] {
            return ::pthread_atfork([] { creating().lock(); }, [] { creating().unlock(); },
                                    [] {
                                        creating().unlock();
                                        current().store(nullptr, std::memory_order_relaxed);
                                    }) == 0;
        }();
        (void)reset_in_child;
#endif
    }

    [[nodiscard]] static std::atomic<parked_workers*>& current() noexcept {
        static std::atomic<parked_workers*> pool{nullptr};
        return pool;
    }
    [[nodiscard]] static std::mutex& creating() noexcept {
        static std::mutex lock;
        return lock;
    }

    void serve() {
        std::uint32_t woken = 0;   // the wake count this thread last saw
        for (;;) {
            m_wake.wait(woken, std::memory_order_acquire);
            woken = m_wake.load(std::memory_order_acquire);
            const job_word drawn(m_job.fetch_add(1, std::memory_order_acq_rel));
            if (drawn.real_ticket()) {
                try {   // a thread must never let an exception out: it would abort the interpreter
                    m_call(m_body, drawn.tickets_taken() + 1);
                } catch (...) {
                    std::lock_guard lock(m_error_mutex);
                    if (!m_error) m_error = std::current_exception();
                }
                m_done.fetch_add(1, std::memory_order_acq_rel);
                m_done.notify_one();
            }
        }
    }

    std::vector<std::thread> m_threads;
    std::atomic<bool> m_busy{false};         // a call holds the pool; an atomic, since a for_each inside body(0) asks again on the same thread
    void* m_body = nullptr;                  // the job: m_call(m_body, worker)
    void (*m_call)(void*, std::size_t) = nullptr;
    std::atomic<std::uint64_t> m_job{0};     // a job_word
    std::atomic<std::uint32_t> m_wake{0};    // parked threads wait on it (32 bits: a futex, see the file comment)
    std::atomic<std::uint32_t> m_done{0};    // helpers of the current job that finished
    std::mutex m_error_mutex;
    std::exception_ptr m_error;              // the first a helper let out, rethrown by the caller
};

/// Calls f(task, worker) for every task in [0, tasks) on `threads` threads (never more than there
/// are tasks), each claiming the next task when it finishes one (rows of a sweep differ a lot in
/// cost). Worker 0 is the calling thread; the others are the parked_workers' threads when they are
/// free, threads of their own otherwise — as many as the system lets start, the caller alone if
/// none. The first exception thrown by any task is rethrown here after every thread has stopped.
template <class F>
void for_each(std::size_t tasks, std::size_t threads, F&& f) {
    threads = std::min(threads, tasks);
    if (threads <= 1) {
        for (std::size_t t = 0; t < tasks; ++t) f(t, std::size_t{0});
        return;
    }
    std::atomic<std::size_t> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr error;
    std::mutex error_mutex;
    auto run = [&](std::size_t worker) {
        for (std::size_t t; !failed.load(std::memory_order_relaxed) && (t = next.fetch_add(1)) < tasks;) {
            try {
                f(t, worker);
            } catch (...) {
                std::lock_guard lock(error_mutex);
                if (!error) error = std::current_exception();
                failed = true;
            }
        }
    };
    if (!parked_workers::process().run(threads - 1, run)) {
        std::vector<std::jthread> own;   // joined as it goes, the caller's share done
        own.reserve(threads - 1);
        try {
            const signals_blocked quiet;   // inherited by the threads started here
            for (std::size_t w = 1; w < threads; ++w) own.emplace_back([&run, w] { name_this_thread("pygim-pool"); run(w); });
        } catch (const std::system_error&) {
        }   // as many as could start: the work is the same, on fewer threads
        run(0);
    }
    if (error) std::rethrow_exception(error);
}

/// A width chosen at run time (Python's threads=): fixed_width or elastic_width.
class chosen_width {
public:
    constexpr chosen_width(fixed_width width) noexcept : m_width(width) {}
    constexpr chosen_width(elastic_width width) noexcept : m_width(width) {}

    [[nodiscard]] constexpr std::size_t at_start() const noexcept {
        return std::visit([](const auto& w) { return w.at_start(); }, m_width);
    }
    [[nodiscard]] constexpr std::size_t for_the_rest(const item_cost& cost, const compute_share& share, std::size_t remaining) const noexcept {
        return std::visit([&](const auto& w) { return w.for_the_rest(cost, share, remaining); }, m_width);
    }

private:
    std::variant<fixed_width, elastic_width> m_width;
};
static_assert(width_policy<chosen_width> && chosen_width(fixed_width(3)).at_start() == 3 && chosen_width(elastic_width(8, 32)).at_start() == 1);

/// What `threads=None` means for an operation, as a policy type: its width on `cores`, and how
/// the refusal of 0 names it. Work that may wait on reads grows its width while more threads help
/// (elastic); work that computes takes one thread per core from the start (Debith, Q39: on 16
/// cores code.parse read CPython's Lib in 75 ms fixed, 145 ms elastic at 20-24 workers).
class grown_while_it_helps {
public:
    static constexpr std::string_view meaning = "a width grown while more threads help";
    [[nodiscard]] static constexpr chosen_width width(std::size_t cores) noexcept { return chosen_width(elastic_width(cores, elastic_cap(cores))); }
};
static_assert(grown_while_it_helps::width(16).at_start() == 1);
class one_per_core {
public:
    static constexpr std::string_view meaning = "one per core";
    [[nodiscard]] static constexpr chosen_width width(std::size_t cores) noexcept { return chosen_width(fixed_width(cores)); }
};
static_assert(one_per_core::width(16).at_start() == 16);
template <class U>
concept unset_policy = requires(std::size_t cores) {
    { U::width(cores) } -> std::same_as<chosen_width>;
    { U::meaning } -> std::convertible_to<std::string_view>;
};

/// The width a `threads=` argument names on a machine of `cores`: none, what `Unset` says; a
/// number, that many threads. 0 is std::invalid_argument naming `who` (in Python, a ValueError).
/// One reading of the argument for every operation that takes it.
///
///     width_for(std::nullopt, "read_each", 16).at_start()            -> 1, growing up to elastic_cap(16)
///     width_for<one_per_core>(std::nullopt, "parse", 16).at_start()  -> 16
///     width_for<one_per_core>(4, "parse", 16).at_start()             -> 4
template <unset_policy Unset = grown_while_it_helps>
[[nodiscard]] constexpr chosen_width width_for(std::optional<std::size_t> threads, std::string_view who, std::size_t cores) {
    if (!threads) return Unset::width(cores);
    if (*threads == 0) throw std::invalid_argument(std::string(who) + ": threads must be at least 1, or None for " + std::string(Unset::meaning));
    return chosen_width(fixed_width(*threads));
}
static_assert(width_for<one_per_core>(std::nullopt, "parse", 16).at_start() == 16 && width_for(std::nullopt, "read_each", 16).at_start() == 1 &&
              width_for<one_per_core>(4, "parse", 16).at_start() == 4);

/// The same on this machine's cores.
template <unset_policy Unset = grown_while_it_helps>
[[nodiscard]] inline chosen_width width_for(std::optional<std::size_t> threads, std::string_view who) {
    return width_for<Unset>(threads, who, this_machines_cores());
}

// ── how far ahead ────────────────────────────────────────────────────────

/// How far workers may run ahead of the consumer: at most `results` results, holding at most
/// `bytes` bytes between them — 0 bytes lets none run ahead of the result the consumer needs next,
/// no_byte_limit leaves the count alone to bound them.
class window_limits {
public:
    static constexpr std::size_t no_byte_limit = std::numeric_limits<std::size_t>::max();

    constexpr window_limits(std::size_t results, std::size_t bytes) noexcept : m_results(std::max<std::size_t>(results, 1)), m_bytes(bytes) {}
    /// A window that holds all of `results` results whatever they weigh: what run_all needs.
    [[nodiscard]] static constexpr window_limits whole(std::size_t results) noexcept { return window_limits(results, no_byte_limit); }

    /// The slots a pool of `tasks` tasks needs: never more than it has tasks.
    [[nodiscard]] constexpr std::size_t slots_for(std::size_t tasks) const noexcept { return std::max<std::size_t>(std::min(m_results, tasks), 1); }
    /// Whether results holding `bytes_ahead` leave room for another.
    [[nodiscard]] constexpr bool admits(std::size_t bytes_ahead) const noexcept { return bytes_ahead < m_bytes; }
    /// Whether the window has drained enough to wake the workers it holds back: by half, in
    /// results (`moved` of `slots`) or in bytes (`freed`). Waking them per result put a system call
    /// on the consumer's path for every one; by too little, every waker woke a herd.
    [[nodiscard]] constexpr bool worth_waking(std::size_t moved, std::size_t freed, std::size_t slots) const noexcept {
        return 2 * moved >= std::max<std::size_t>(slots, 2) || (m_bytes != 0 && freed >= m_bytes / 2);
    }

private:
    std::size_t m_results;
    std::size_t m_bytes;
};
static_assert(window_limits(1'024, 100).slots_for(10) == 10 && window_limits(4, 100).slots_for(10) == 4 && window_limits(4, 100).slots_for(0) == 1);
static_assert(window_limits(4, 100).admits(99) && !window_limits(4, 100).admits(100) && !window_limits(4, 0).admits(0) &&
              window_limits::whole(4).admits(window_limits::no_byte_limit - 1) && window_limits::whole(10).slots_for(10) == 10);
static_assert(window_limits(1'024, 1'000).worth_waking(512, 0, 1'024) && !window_limits(1'024, 1'000).worth_waking(511, 0, 1'024) &&
              window_limits(1'024, 1'000).worth_waking(1, 500, 1'024) && window_limits(1, 1'000).worth_waking(1, 0, 1));

/// What a result weighs against window_limits' bytes: nothing, unless the pool is told.
class unweighed {
public:
    template <class Result>
    [[nodiscard]] constexpr std::size_t operator()(const Result&) const noexcept { return 0; }
};
static_assert(unweighed{}(42) == 0);

// ── what a pool learns from its tasks ───────────────────────────────────

/// What a pool learns from its finished tasks — their time, their count, and on every 16th the
/// share its thread spent on a CPU — and the one decision it takes from them: how many threads the
/// rest of its tasks is worth, by its width policy. ordered_pool and task_pool each hold one.
template <clock_source Clock, clock_source CpuClock>
class task_meter {
public:
    static constexpr std::size_t sampled_every = 16;

    task_meter(Clock clock, CpuClock cpu_clock) noexcept : m_clock(clock), m_cpu_clock(cpu_clock) {}

    /// f(), its time added to what the tasks cost whether it returns or throws; the `k`-th task a
    /// pool started is sampled on the CPU clock when k is a multiple of sampled_every.
    template <class F>
    decltype(auto) timed(std::size_t k, F&& f) {
        class spent {
        public:
            spent(task_meter& meter, bool sampled) noexcept
                : m_meter(meter), m_sampled(sampled), m_cpu_before(sampled ? meter.m_cpu_clock.now_ns() : 0), m_before(meter.m_clock.now_ns()) {}
            ~spent() {
                const std::int64_t took = m_meter.m_clock.now_ns() - m_before;
                if (m_sampled) {
                    m_meter.m_sampled_cpu_ns.fetch_add(m_meter.m_cpu_clock.now_ns() - m_cpu_before, std::memory_order_relaxed);
                    m_meter.m_sampled_wall_ns.fetch_add(took, std::memory_order_relaxed);
                }
                m_meter.m_spent_ns.fetch_add(took, std::memory_order_relaxed);
            }

        private:
            task_meter& m_meter;
            bool m_sampled;
            std::int64_t m_cpu_before, m_before;
        } after(*this, k % sampled_every == 0);
        return f();
    }

    /// One more task is done — returned or thrown; how many are now.
    std::size_t count_done() noexcept { return m_done.fetch_add(1, std::memory_order_acq_rel) + 1; }

    /// grow(the threads `width` says `remaining` more tasks are worth), from one thread at a time —
    /// others go on with their tasks. Best effort: when a thread cannot start, those running finish
    /// the work, so whatever grow throws is dropped here.
    template <width_policy Width, class Grow>
    void consider(const Width& width, std::size_t remaining, Grow&& grow) noexcept {
        if (m_growing.exchange(true, std::memory_order_acquire)) return;
        const item_cost cost(duration(m_spent_ns.load(std::memory_order_relaxed)), m_done.load(std::memory_order_relaxed));
        const compute_share share(duration(m_sampled_cpu_ns.load(std::memory_order_relaxed)), duration(m_sampled_wall_ns.load(std::memory_order_relaxed)));
        try {
            grow(width.for_the_rest(cost, share, remaining));
        } catch (...) {
        }
        m_growing.store(false, std::memory_order_release);
    }

private:
    [[no_unique_address]] Clock m_clock;
    [[no_unique_address]] CpuClock m_cpu_clock;
    std::atomic<std::int64_t> m_spent_ns{0};          // the finished tasks' time
    std::atomic<std::size_t> m_done{0};               // how many finished
    std::atomic<std::int64_t> m_sampled_wall_ns{0};   // the sampled tasks' time ...
    std::atomic<std::int64_t> m_sampled_cpu_ns{0};    // ... and how much of it their threads spent on a CPU
    std::atomic<bool> m_growing{false};               // a thread is deciding
};

// ── the pool ─────────────────────────────────────────────────────────────

/// n tasks, task(i) for each i below n, their results handed to one consumer in index order. Clock
/// times the tasks; CpuClock, read on every 16th, says how much of that time was spent computing.
template <class Task, width_policy Width = chosen_width, class Weigh = unweighed, clock_source Clock = monotonic_clock,
          clock_source CpuClock = thread_cpu_clock>
class ordered_pool {
public:
    using result_type = std::invoke_result_t<const Task&, std::size_t>;

    ordered_pool(std::size_t n, Task task, Width width, window_limits limits, Weigh weigh = Weigh{}, Clock clock = Clock{},
                 CpuClock cpu_clock = CpuClock{})
        : m_n(n), m_task(std::move(task)), m_width(std::move(width)), m_limits(limits), m_weigh(std::move(weigh)),
          m_meter(clock, cpu_clock), m_slots(limits.slots_for(n)) {
        for (std::size_t k = 0; k < m_slots.size(); ++k) m_slots[k].open_for(k);
    }
    /// Stops the workers and waits for them: a task already running finishes, no other starts.
    ~ordered_pool() { stop(); }
    ordered_pool(const ordered_pool&) = delete;
    ordered_pool& operator=(const ordered_pool&) = delete;

    /// Runs every task on workers, none in the caller, and calls `finished()` — which must not
    /// throw — from the worker that stores the last result, a task that threw counted too (at once
    /// when there are none). Returns at once: the caller sleeps as it likes — an interrupt_scope's
    /// wait() — then takes the results with next(), each ready, or destroys the pool to stop it.
    /// The window must hold every result, since nobody takes one until the end, and so holds them
    /// whatever they weigh. std::system_error when not one worker can start. Call it once, before
    /// any next().
    void run_all(std::function<void()> finished) {
        if (m_slots.size() < m_n) throw std::logic_error("run_all needs a window that holds every result");
        m_limits = window_limits::whole(m_n);   // a byte limit would hold back the results nobody takes yet
        m_caller_works = false;
        m_finished = std::move(finished);   // before any worker exists: none can finish unheard
        m_started = true;
        if (m_n == 0) {
            m_finished();
            return;
        }
        try {
            grow_to(m_width.at_start());
        } catch (const std::system_error&) {
            if (m_workers_started.load(std::memory_order_relaxed) == 0) throw;   // fewer workers do the same work; none, none of it
        }
    }

    /// Whether every result has been handed out.
    [[nodiscard]] bool exhausted() const noexcept { return m_out.load(std::memory_order_relaxed) >= m_n; }
    /// Whether next() would hand a result over at once: a worker has it ready, nothing to wait for
    /// or to run — what lets a caller holding a lock (Python's GIL) keep it.
    [[nodiscard]] bool ready() const noexcept {
        const std::size_t j = m_out.load(std::memory_order_relaxed);
        return j < m_n && m_slots[j % m_slots.size()].is_full_for(j);
    }

    /// The next result in index order: run here when nobody has started it, else waited for. A
    /// task's exception is rethrown here, at its place in the order; std::out_of_range once every
    /// result has been handed out.
    [[nodiscard]] result_type next() {
        const std::size_t j = m_out.load(std::memory_order_relaxed);
        if (j >= m_n) throw std::out_of_range("ordered_pool: every result was handed out");
        if (!m_started) {   // the workers start with the first result asked for, not with the pool
            m_started = true;
            try {
                grow_to(m_width.at_start());
            } catch (const std::system_error&) {
            }   // best effort: the consumer works, whatever starts beside it
        }
        std::size_t unclaimed = j;
        if (m_next_claim.compare_exchange_strong(unclaimed, j + 1)) return run_here(j);
        return take(j);
    }

private:
    // A task's place in the window: free for task j, full once a worker has put task j's result or
    // error in it, then emptied by the consumer and freed for task j + slots. Its state, modulo
    // 2^32, is what a waiter compares with: 2j free for task j, 2j + 1 full with it.
    class slot {
    public:
        // What a full slot held: its task's result, or what the task threw; its weight in the window.
        class outcome {
        public:
            outcome(std::optional<result_type> result, std::exception_ptr error, std::size_t weight) noexcept
                : m_result(std::move(result)), m_error(std::move(error)), m_weight(weight) {}
            [[nodiscard]] std::size_t weight() const noexcept { return m_weight; }
            /// The result; what the task threw, rethrown.
            [[nodiscard]] result_type get() && {
                if (m_error) std::rethrow_exception(m_error);
                return std::move(*m_result);
            }

        private:
            std::optional<result_type> m_result;
            std::exception_ptr m_error;
            std::size_t m_weight;
        };

        static constexpr std::uint32_t free_for(std::size_t j) noexcept { return static_cast<std::uint32_t>(2 * j); }
        static constexpr std::uint32_t full_for(std::size_t j) noexcept { return static_cast<std::uint32_t>(2 * j + 1); }

        [[nodiscard]] bool is_free_for(std::size_t j) const noexcept { return m_state.load(std::memory_order_acquire) == free_for(j); }
        [[nodiscard]] bool is_full_for(std::size_t j) const noexcept { return m_state.load(std::memory_order_acquire) == full_for(j); }
        /// What a waiter waits on.
        [[nodiscard]] std::atomic<std::uint32_t>& state() noexcept { return m_state; }

        /// Free for task j, nothing in it.
        void open_for(std::size_t j) noexcept { m_state.store(free_for(j), std::memory_order_release); }
        /// Task j's result in, `weight` bytes of the window; whoever waits for it is woken.
        void fill(std::size_t j, result_type result, std::size_t weight) {
            m_result.emplace(std::move(result));
            m_weight = weight;
            publish(j);
        }
        /// What task j threw, in its result's place.
        void fail(std::size_t j, std::exception_ptr error) noexcept {
            m_error = std::move(error);
            publish(j);
        }
        /// What it holds, taken out: it holds nothing after.
        [[nodiscard]] outcome empty() noexcept {
            return outcome(std::exchange(m_result, std::nullopt), std::exchange(m_error, nullptr), std::exchange(m_weight, 0));
        }

    private:
        void publish(std::size_t j) noexcept {
            m_state.store(full_for(j), std::memory_order_release);
            m_state.notify_all();
        }

        std::atomic<std::uint32_t> m_state{0};
        std::optional<result_type> m_result;
        std::exception_ptr m_error;
        std::size_t m_weight = 0;
    };
    static_assert(slot::free_for(3) == 6 && slot::full_for(3) == 7 && slot::free_for(std::size_t{1} << 31) == 0);   // modulo 2^32

    [[nodiscard]] slot& at(std::size_t j) noexcept { return m_slots[j % m_slots.size()]; }

    // Waits until `a` holds `want`; false when the pool stops first.
    [[nodiscard]] bool wait_for(std::atomic<std::uint32_t>& a, std::uint32_t want) const noexcept {
        for (std::uint32_t s = a.load(std::memory_order_acquire); s != want; s = a.load(std::memory_order_acquire)) {
            if (m_stopping.load(std::memory_order_acquire)) return false;
            a.wait(s, std::memory_order_acquire);
        }
        return true;
    }

    // Task j may run once its slot is free and the window has room; the task the consumer needs next
    // always may, so the window never stalls the consumer. A worker the window holds back sleeps
    // until the window has drained by half (announce()): waking one per result handed out put a
    // system call on the consumer's path for every result — 20,000 small files took 144 ms on 16
    // threads instead of 11 once the workers had filled the window.
    [[nodiscard]] bool admitted(std::size_t j) {
        for (;;) {
            const std::uint32_t seen = m_moved.load(std::memory_order_acquire);   // before the checks: no move is missed
            if (m_stopping.load(std::memory_order_acquire)) return false;
            const bool slot_free = at(j).is_free_for(j);
            const bool has_room = j == m_out.load(std::memory_order_acquire) || m_limits.admits(m_bytes_ahead.load(std::memory_order_acquire));
            if (slot_free && has_room) return true;
            m_moved.wait(seen, std::memory_order_acquire);
        }
    }

    // One worker: claims tasks in order until there are none, or the pool stops.
    void work() {
        for (std::size_t j; (j = m_next_claim.fetch_add(1)) < m_n;) {
            if (!admitted(j)) return;
            slot& s = at(j);
            try {
                result_type result = m_meter.timed(j, [&] { return m_task(j); });
                const std::size_t weight = m_weigh(result);
                m_bytes_ahead.fetch_add(weight, std::memory_order_acq_rel);   // before it is full: the consumer subtracts it after
                s.fill(j, std::move(result), weight);
            } catch (...) {
                s.fail(j, std::current_exception());
            }
            finished_one();   // after its result is in: run_all's finished() finds every result ready
        }
    }

    // A task is done, returned or thrown: its cost may start more workers, and the last one wakes
    // run_all's caller.
    void finished_one() noexcept {
        const std::size_t done = m_meter.count_done();
        const std::size_t remaining = m_n - std::min(m_next_claim.load(std::memory_order_relaxed), m_n);
        m_meter.consider(m_width, remaining, [this](std::size_t w) { grow_to(w); });
        if (done == m_n && m_finished) m_finished();
    }

    // Workers until `threads` threads run — the consumer counted as one while it works too (next()),
    // not under run_all — never more than half the window's slots, since no more can be let in at
    // once when it drains by half: a 16-slot window on 16 threads took 215 ms for 20,000 small
    // files, on 8 threads 18 ms. A window that holds every result (run_all's) has no such limit.
    void grow_to(std::size_t wanted) {
        const std::size_t room = m_slots.size() >= m_n ? wanted : std::max<std::size_t>(m_slots.size() / 2, 1);
        const std::size_t threads = std::min({wanted, room, std::max<std::size_t>(m_n, 1)});   // never more threads than tasks
        const std::size_t caller = m_caller_works ? 1 : 0;
        if (threads <= m_workers_started.load(std::memory_order_relaxed) + caller) return;
        std::lock_guard guard(m_spawn);
        class counted {   // those that did start, even when the next cannot
        public:
            explicit counted(ordered_pool& pool) noexcept : m_pool(pool) {}
            ~counted() { m_pool.m_workers_started.store(m_pool.m_workers.size(), std::memory_order_relaxed); }

        private:
            ordered_pool& m_pool;
        } after(*this);
        const signals_blocked quiet;   // inherited by the workers started here
        while (!m_stopping.load(std::memory_order_relaxed) && m_workers.size() + caller < threads) {
            m_workers.emplace_back([this] { name_this_thread("pygim-pool"); work(); });
        }
    }

    // Task j, in the consumer: handed out and counted done, returned or thrown.
    [[nodiscard]] result_type run_here(std::size_t j) {
        class moves_on {
        public:
            moves_on(ordered_pool& pool, std::size_t j) noexcept : m_pool(pool), m_j(j) {}
            ~moves_on() {
                m_pool.release(m_j, 0);
                m_pool.finished_one();
            }

        private:
            ordered_pool& m_pool;
            std::size_t m_j;
        } after(*this, j);
        return m_meter.timed(j, [&] { return m_task(j); });
    }

    // Task j's result, from its slot.
    [[nodiscard]] result_type take(std::size_t j) {
        slot& s = at(j);
        if (!s.is_full_for(j)) announce();   // about to wait: no worker may sleep on a move not told
        (void)wait_for(s.state(), slot::full_for(j));   // the consumer is never waiting when the pool stops
        typename slot::outcome got = s.empty();   // before the slot is freed for a later task
        m_bytes_ahead.fetch_sub(got.weight(), std::memory_order_acq_rel);
        release(j, got.weight());
        return std::move(got).get();
    }

    // Task j is handed out, freeing `freed` bytes: its slot is task j + slots' to fill, and the window
    // has moved — told to the workers it holds back once it has drained by half.
    void release(std::size_t j, std::size_t freed) noexcept {
        at(j).open_for(j + m_slots.size());
        m_out.store(j + 1, std::memory_order_release);
        m_unannounced_results += 1;
        m_unannounced_bytes += freed;
        if (m_limits.worth_waking(m_unannounced_results, m_unannounced_bytes, m_slots.size())) announce();
    }

    // Wakes the workers the window holds back, to look again.
    void announce() noexcept {
        m_unannounced_results = 0;
        m_unannounced_bytes = 0;
        m_moved.fetch_add(1, std::memory_order_acq_rel);
        m_moved.notify_all();
    }

    // Every waiter woken to find the pool stopping, then every worker joined.
    void stop() noexcept {
        {
            std::lock_guard guard(m_spawn);
            m_stopping.store(true, std::memory_order_release);
        }
        m_moved.fetch_add(1, std::memory_order_acq_rel);
        m_moved.notify_all();
        std::vector<std::jthread> workers;
        {
            std::lock_guard guard(m_spawn);
            workers = std::move(m_workers);
        }
    }   // joined here

    std::size_t m_n;
    Task m_task;
    Width m_width;
    window_limits m_limits;
    [[no_unique_address]] Weigh m_weigh;
    task_meter<Clock, CpuClock> m_meter;
    std::vector<slot> m_slots;
    std::atomic<std::size_t> m_next_claim{0};     // the next task nobody has started
    std::atomic<std::size_t> m_out{0};            // the next result the consumer takes
    std::atomic<std::size_t> m_bytes_ahead{0};    // what the results waiting in slots weigh
    std::size_t m_unannounced_results = 0;        // the consumer's: what has drained since the last wake-up
    std::size_t m_unannounced_bytes = 0;
    std::atomic<std::uint32_t> m_moved{0};        // moves at each wake-up: what those workers wait on
    std::atomic<bool> m_stopping{false};
    std::atomic<std::size_t> m_workers_started{0};
    bool m_started = false;                       // the consumer's: workers start at run_all or the first next()
    bool m_caller_works = true;                   // false under run_all: every task on workers
    std::function<void()> m_finished;             // run_all's: called once the last task is done
    std::mutex m_spawn;
    std::vector<std::jthread> m_workers;
};

// ── a pool for work found as it is done ──────────────────────────────────

/// Tasks that make more tasks — a walk finds directories as it lists them — run on as many
/// threads as the work rewards: run(task, add) does one, and add(task) queues another. The pool
/// is done when nothing is queued and nothing runs. Its threads grow as ordered_pool's do (the
/// width policy, each task's cost by Clock, the share of it on a CPU by CpuClock), with what is
/// queued as the rest of the work. Tasks are handed out first in, first out through one lock: a
/// walk of 100,000 files in 1,000 directories still scaled 7.8x to 32 threads that way (#274);
/// per-thread queues with stealing are the next step for trees much larger.
///
///     task_pool pool([&](dir_task t, const auto& add) { for (auto& sub : list(t)) add(sub); }, width);
///     pool.submit(root);
///     pool.run_here();                    // the caller works too, until every task is done
///     pool.run_all([&] { scope.finish(); });   // or workers only, telling the caller when done
template <class Task, class Run, width_policy Width = chosen_width, clock_source Clock = monotonic_clock,
          clock_source CpuClock = thread_cpu_clock>
class task_pool {
public:
    /// What a running task is given to add tasks with.
    class adder {
    public:
        explicit adder(task_pool* pool) noexcept : m_pool(pool) {}
        void operator()(Task task) const { m_pool->submit(std::move(task)); }

    private:
        task_pool* m_pool;
    };

    task_pool(Run run, Width width, Clock clock = Clock{}, CpuClock cpu_clock = CpuClock{})
        : m_run(std::move(run)), m_width(std::move(width)), m_meter(clock, cpu_clock) {}
    /// Stops the workers and waits for them: a task already running finishes, no other starts.
    ~task_pool() { stop(); }
    task_pool(const task_pool&) = delete;
    task_pool& operator=(const task_pool&) = delete;

    /// Queues a task: before a run, or from a running task through its adder.
    void submit(Task task) {
        {
            std::lock_guard guard(m_lock);
            m_queue.push_back(std::move(task));
            ++m_pending;
        }
        m_wake.notify_one();
    }

    /// Runs every task, the caller among the workers — the caller alone when no other thread can
    /// start — and returns when none is left; a task's exception is rethrown here once the others
    /// have finished.
    void run_here() {
        try {
            grow_to(m_width.at_start());
        } catch (const std::system_error&) {
        }   // best effort: the caller works, whatever starts beside it
        work();
        rethrow_if_failed();
    }

    /// Runs every task on workers, none in the caller, and calls `finished()` — which must not
    /// throw, and may call rethrow_if_failed() — from the worker that ends the last one (at once
    /// when there is none). Returns at once: the caller waits as it likes, then calls
    /// rethrow_if_failed(), or destroys the pool to stop it. std::system_error when not one worker
    /// can start.
    void run_all(std::function<void()> finished) {
        bool nothing = false;
        {
            std::lock_guard guard(m_lock);
            m_caller_works = false;
            m_finished = std::move(finished);
            nothing = m_pending == 0;
        }
        if (nothing) {
            m_finished();   // outside the lock, which it may take
            return;
        }
        try {
            grow_to(m_width.at_start());
        } catch (const std::system_error&) {
            if (m_workers_started.load(std::memory_order_relaxed) == 0) throw;   // fewer workers do the same work; none, none of it
        }
    }

    /// A task's exception, rethrown: the first one, if any task threw.
    void rethrow_if_failed() const {
        std::lock_guard guard(m_lock);
        if (m_error) std::rethrow_exception(m_error);
    }

private:
    // One worker (or the caller, under run_here): tasks until none is queued or running. Whatever
    // escapes a task's bookkeeping (a lock that cannot be taken, say) becomes the pool's error and
    // ends the run for everyone, so no caller waits on tasks that will never end.
    void work() {
        try {
            for (;;) {
                std::optional<Task> task;
                {
                    std::unique_lock guard(m_lock);
                    m_wake.wait(guard, [&] { return m_stopping || !m_queue.empty() || m_pending == 0; });
                    if (m_stopping || m_queue.empty()) return;   // stopped, or every task done
                    task.emplace(std::move(m_queue.front()));
                    m_queue.pop_front();
                }
                run_one(std::move(*task));
            }
        } catch (...) {
            {
                std::lock_guard guard(m_lock);
                if (!m_error) m_error = std::current_exception();
                m_stopping = true;
            }
            m_wake.notify_all();
            if (m_finished) m_finished();
        }
    }

    // One task, timed; then the count of what is left, and maybe more workers.
    void run_one(Task&& task) {
        try {
            m_meter.timed(m_started.fetch_add(1, std::memory_order_relaxed), [&] { m_run(std::move(task), adder(this)); });
        } catch (...) {
            std::lock_guard guard(m_lock);
            if (!m_error) m_error = std::current_exception();
        }
        m_meter.count_done();
        bool all_done = false;
        std::size_t queued = 0;
        {
            std::lock_guard guard(m_lock);
            all_done = --m_pending == 0;   // the tasks this one added were counted before it ends
            queued = m_queue.size();
        }
        if (all_done) {
            m_wake.notify_all();
            if (m_finished) m_finished();   // run_all's caller wakes
            return;
        }
        m_meter.consider(m_width, queued, [this](std::size_t w) { grow_to(w); });
    }

    // Workers until `threads` threads run, the caller counted as one under run_here; nothing to
    // lock for when as many run already, as after the first few tasks they do.
    void grow_to(std::size_t threads) {
        const std::size_t caller = m_caller_works ? 1 : 0;
        if (threads <= m_workers_started.load(std::memory_order_relaxed) + caller) return;
        std::lock_guard guard(m_spawn);
        class counted {   // those that did start, even when the next cannot
        public:
            explicit counted(task_pool& pool) noexcept : m_pool(pool) {}
            ~counted() { m_pool.m_workers_started.store(m_pool.m_workers.size(), std::memory_order_relaxed); }

        private:
            task_pool& m_pool;
        } after(*this);
        const signals_blocked quiet;   // inherited by the workers started here
        while (!m_stopping_spawn && m_workers.size() + caller < threads) {
            m_workers.emplace_back([this] { name_this_thread("pygim-pool"); work(); });
        }
    }

    void stop() noexcept {
        {
            std::lock_guard spawn(m_spawn);
            m_stopping_spawn = true;
        }
        {
            std::lock_guard guard(m_lock);
            m_stopping = true;
        }
        m_wake.notify_all();
        std::vector<std::jthread> workers;
        {
            std::lock_guard spawn(m_spawn);
            workers = std::move(m_workers);
        }
    }   // joined here

    Run m_run;
    Width m_width;
    task_meter<Clock, CpuClock> m_meter;
    mutable std::mutex m_lock;                    // the queue, the count of what is left, the first error
    std::condition_variable m_wake;
    std::deque<Task> m_queue;
    std::size_t m_pending = 0;                    // queued and running
    bool m_stopping = false;
    bool m_caller_works = true;
    std::function<void()> m_finished;
    std::exception_ptr m_error;
    std::atomic<std::size_t> m_started{0};        // tasks started: which the meter samples
    std::mutex m_spawn;                           // the workers vector
    bool m_stopping_spawn = false;
    std::atomic<std::size_t> m_workers_started{0};
    std::vector<std::jthread> m_workers;
};

}  // namespace pygim::parallel
