// tests/static/threads_stress.cpp — the process-wide machinery several threads share, driven hard
// under ThreadSanitizer, or AddressSanitizer with UndefinedBehaviorSanitizer: the parked pool
// (utils/parallel.h), the kept blocks (utils/kept_blocks.h) and the interrupt scopes (utils/interrupt.h).
//
// Built and run by run_threads_stress.py, once per sanitizer. Each part prints one line and the
// process exits 0 when every part held; a sanitizer report ends it first (-fno-sanitize-recover).
//
//   pool      for_each: every task once, workers in range, a for_each inside a task, four threads
//             calling at once, an exception rethrown by the caller every time
//   blocks    eight threads taking and giving blocks of a few size classes, each writing its own
//             pattern over the block and finding it intact before giving it back — a block lent
//             twice, or freed while lent, shows as a changed pattern or a sanitizer report
//   fork      a child forked while other threads take and give blocks and run for_each, which
//             then uses both: a lock the fork left held, or a pool whose threads stayed behind,
//             hangs it, and the parent gives up after five seconds
//   ordered   ordered_pool: a task that throws still counts as done, so run_all's finished()
//             fires and next() rethrows at its place; run_all's window holds every result
//             whatever its byte limit; next() past the last result refuses
//   tasks     task_pool: run_all of nothing may be told from a finished() that asks for errors;
//             a task's exception reaches run_here's caller after the others ran
//   caller    parked_workers::run: when the caller's own share throws, every helper that took a
//             ticket has left the body before the exception reaches the caller
//   starved   a child that may start no thread at all: for_each runs on the caller, task_pool's
//             run_here too, and ordered_pool's run_all says it could not start
//   scopes    interrupt_scope (utils/interrupt.h): 64 held at once on as many threads; a 65th is
//             refused and leaves the handlers installed; one SIGINT wakes all 64; the last out gives
//             the program's handler back and raises the signal to it once
//
// A part that hangs is a failure too: each runs under an alarm.

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <random>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "../../src/_pygim_fast/utils/interrupt.h"
#include "../../src/_pygim_fast/utils/kept_blocks.h"
#include "../../src/_pygim_fast/utils/parallel.h"

namespace {

using pygim::kept_blocks;
using pygim::parallel::for_each;
namespace parallel = pygim::parallel;

const char* g_part = "";   // the part running, for the alarm to name

void on_alarm(int) {
    char line[128];
    const int n = std::snprintf(line, sizeof line, "%s: hung\n", g_part);
    (void)!::write(2, line, static_cast<std::size_t>(n));
    ::_exit(3);
}

// Runs `part` under an alarm of `seconds`: a hang ends the process naming the part.
template <class Part>
bool timed_part(const char* name, unsigned seconds, Part&& part) {
    g_part = name;
    ::signal(SIGALRM, on_alarm);
    ::alarm(seconds);
    const bool ok = part();
    ::alarm(0);
    return ok;
}

// What a pool test needs of a width: a fixed number of threads.
parallel::chosen_width threads(std::size_t n) { return parallel::chosen_width(parallel::fixed_width(n)); }

bool pool_round(std::mt19937& rng, int depth) {
    const std::size_t tasks = rng() % 200, threads = 1 + rng() % 20;
    std::vector<std::atomic<int>> ran(tasks);
    std::atomic<bool> ok{true};
    for_each(tasks, threads, [&](std::size_t t, std::size_t w) {
        if (w >= threads) ok = false;
        ran[t].fetch_add(1);
        if (depth == 0 && t % 37 == 0) {
            std::mt19937 inner(static_cast<unsigned>(t));
            if (!pool_round(inner, 1)) ok = false;
        }
    });
    for (auto& r : ran) ok = ok && r.load() == 1;
    return ok;
}

bool pool() {
    std::atomic<int> failures{0};
    std::vector<std::thread> callers;
    for (int c = 0; c < 4; ++c) {
        callers.emplace_back([&, c] {
            std::mt19937 rng(static_cast<unsigned>(c));
            for (int k = 0; k < 400; ++k) failures += !pool_round(rng, 0);
        });
    }
    for (auto& t : callers) t.join();
    int thrown = 0;
    for (int k = 0; k < 200; ++k) {
        try {
            for_each(64, 16, [](std::size_t t, std::size_t) {
                if (t == 13) throw std::runtime_error("task 13");
            });
        } catch (const std::runtime_error&) {
            ++thrown;
        }
    }
    std::printf("pool    %d failed rounds of 1600, %d of 200 exceptions rethrown\n", failures.load(), thrown);
    return failures == 0 && thrown == 200;
}

/// Sizes around a few classes, so threads share them and the shelf both reuses and drops blocks.
std::size_t some_size(std::mt19937& rng) {
    constexpr std::size_t mb = std::size_t{1} << 20;
    const std::size_t base[] = {mb, 3 * mb / 2, 5 * mb, 40 * mb};
    return base[rng() % 4] - rng() % 4096;
}

/// One thread's rounds: take, write its pattern over every page, give back after checking it.
bool blocks_by(unsigned seed, int rounds) {
    std::mt19937 rng(seed);
    for (int k = 0; k < rounds; ++k) {
        const std::size_t bytes = some_size(rng);
        auto* p = static_cast<unsigned char*>(kept_blocks::process().take(bytes));
        const auto mark = static_cast<unsigned char>(seed * 31 + static_cast<unsigned>(k));
        for (std::size_t i = 0; i < bytes; i += 4096) p[i] = mark;
        p[bytes - 1] = mark;
        std::this_thread::yield();
        for (std::size_t i = 0; i < bytes; i += 4096) {
            if (p[i] != mark) return false;
        }
        if (p[bytes - 1] != mark) return false;
        kept_blocks::process().give(p, bytes);
    }
    return true;
}

bool blocks() {
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 8; ++t) threads.emplace_back([&, t] { failures += !blocks_by(t + 1, 300); });
    for (auto& t : threads) t.join();
    const std::size_t held = kept_blocks::process().held();
    std::printf("blocks  %d of 8 threads saw a block changed under them; %zu MB kept, at most %zu\n", failures.load(), held >> 20,
                kept_blocks::held_at_most >> 20);
    return failures == 0 && held <= kept_blocks::held_at_most;
}

bool fork_while_busy() {
    std::atomic<bool> stop{false};
    std::vector<std::thread> busy;
    for (unsigned t = 0; t < 4; ++t) {
        busy.emplace_back([&, t] {
            std::mt19937 rng(t + 100);
            while (!stop.load()) {
                if (!blocks_by(rng(), 1)) std::abort();
                for_each(32, 4, [](std::size_t, std::size_t) {});
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int hung = 0, failed = 0;
    for (int k = 0; k < 20; ++k) {
        const pid_t pid = ::fork();
        if (pid == 0) {
            std::atomic<int> sum{0};
            for_each(64, 16, [&](std::size_t t, std::size_t) { sum += static_cast<int>(t); });
            ::_exit(blocks_by(7, 20) && sum == 2016 ? 0 : 1);
        }
        int status = 0;
        bool done = false;
        for (int wait = 0; wait < 500 && !done; ++wait) {   // five seconds
            if (::waitpid(pid, &status, WNOHANG) == pid) done = true;
            else ::usleep(10000);
        }
        if (!done) {
            ++hung;
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
        } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            ++failed;
        }
    }
    stop = true;
    for (auto& t : busy) t.join();
    std::printf("fork    %d of 20 children hung, %d failed\n", hung, failed);
    return hung == 0 && failed == 0;
}

bool ordered() {
    constexpr std::size_t n = 32;
    // a task that throws: run_all's finished() still fires, and next() rethrows at its place
    auto task = [](std::size_t j) -> std::size_t {
        if (j == 7) throw std::runtime_error("task 7");
        return j * j;
    };
    std::atomic<bool> finished{false};
    parallel::ordered_pool pool(n, task, threads(4), parallel::window_limits(n, parallel::window_limits::no_byte_limit));
    pool.run_all([&] { finished.store(true); finished.notify_all(); });
    finished.wait(false);
    bool in_order = true, rethrown = false;
    for (std::size_t j = 0; j < n; ++j) {
        try {
            in_order = in_order && pool.next() == j * j && j != 7;
        } catch (const std::runtime_error&) {
            rethrown = j == 7;
        }
    }
    bool refused_past_the_end = false;
    try {
        (void)pool.next();
    } catch (const std::out_of_range&) {
        refused_past_the_end = true;
    }
    // run_all with a byte limit smaller than one result: the window holds every result anyway
    class heavy {
    public:
        std::size_t operator()(std::size_t) const noexcept { return 1000; }
    };
    std::atomic<bool> all{false};
    parallel::ordered_pool weighed(n, [](std::size_t j) { return j; }, threads(4), parallel::window_limits(n, 1), heavy{});
    weighed.run_all([&] { all.store(true); all.notify_all(); });
    all.wait(false);
    std::size_t sum = 0;
    while (!weighed.exhausted()) sum += weighed.next();
    std::printf("ordered %s, %s, %s, %s\n", in_order ? "results in order" : "RESULTS OUT OF ORDER", rethrown ? "task 7 rethrown" : "TASK 7 LOST",
                refused_past_the_end ? "past the end refused" : "PAST THE END NOT REFUSED", sum == n * (n - 1) / 2 ? "a byte-limited run_all ends" : "BYTE-LIMITED RUN_ALL WRONG");
    return in_order && rethrown && refused_past_the_end && sum == n * (n - 1) / 2;
}

bool tasks() {
    // run_all of nothing: finished() is called at once, and may ask for the errors
    bool asked = false;
    {
        auto run = [](int, const auto&) {};
        parallel::task_pool<int, decltype(run)> pool(run, threads(4));
        pool.run_all([&] {
            pool.rethrow_if_failed();
            asked = true;
        });
    }
    // a task that throws: the others still run, and run_here's caller gets the exception
    std::atomic<int> ran{0};
    bool thrown = false;
    {
        auto run = [&](int t, const auto& add) {
            ran.fetch_add(1);
            if (t < 8) add(t + 8);
            if (t == 5) throw std::runtime_error("task 5");
        };
        parallel::task_pool<int, decltype(run)> pool(run, threads(4));
        for (int t = 0; t < 8; ++t) pool.submit(t);
        try {
            pool.run_here();
        } catch (const std::runtime_error&) {
            thrown = true;
        }
    }
    std::printf("tasks   %s, %s; %d of 16 tasks ran\n", asked ? "finished() may ask for errors" : "FINISHED() DID NOT RETURN", thrown ? "task 5 rethrown" : "TASK 5 LOST",
                ran.load());
    return asked && thrown && ran.load() == 16;
}

bool caller_throws() {
    // the caller's share throws while helpers are still inside the body: none may be left there
    std::atomic<int> inside{0}, entered{0};
    auto body = [&](std::size_t worker) {
        if (worker == 0) {
            while (entered.load() == 0) std::this_thread::yield();
            throw std::runtime_error("the caller's share");
        }
        inside.fetch_add(1);
        entered.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        inside.fetch_sub(1);
    };
    bool thrown = false;
    int left_inside = -1;
    for (int k = 0; k < 20; ++k) {
        try {
            if (!parallel::parked_workers::process().run(3, body)) return false;   // the pool is free here
        } catch (const std::runtime_error&) {
            thrown = true;
            left_inside = std::max(left_inside, inside.load());
        }
        while (inside.load() != 0) std::this_thread::yield();
        entered = 0;
    }
    std::printf("caller  %s; at most %d helper(s) still in the body when it arrived\n", thrown ? "the exception reached the caller" : "NO EXCEPTION", left_inside);
    return thrown && left_inside == 0;
}

bool starved() {
    const pid_t pid = ::fork();
    if (pid == 0) {
        const rlimit none{0, 0};   // no new thread or process for this user from here on
        ::setrlimit(RLIMIT_NPROC, &none);
        std::atomic<int> sum{0};
        for_each(64, 16, [&](std::size_t t, std::size_t) { sum += static_cast<int>(t); });
        std::atomic<int> ran{0};
        auto run = [&](int, const auto&) { ran.fetch_add(1); };
        parallel::task_pool<int, decltype(run)> pool(run, threads(4));
        for (int t = 0; t < 8; ++t) pool.submit(t);
        pool.run_here();
        bool refused = false;
        try {
            parallel::ordered_pool workers(4, [](std::size_t j) { return j; }, threads(4), parallel::window_limits(4, parallel::window_limits::no_byte_limit));
            workers.run_all([] {});
        } catch (const std::system_error&) {
            refused = true;
        }
        ::_exit(sum == 2016 && ran == 8 && refused ? 0 : 1);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    std::printf("starved %s\n", ok ? "without threads, everything ran on the caller and run_all said it could not start"
                                    : WIFSIGNALED(status) ? "THE CHILD DIED" : "THE CHILD FAILED");
    return ok;
}

std::atomic<int> g_theirs{0};   // how often the program's own SIGINT handler ran
void program_handler(int) { g_theirs.fetch_add(1); }

[[nodiscard]] bool handler_is(void (*expected)(int)) {
    struct sigaction now {};
    ::sigaction(SIGINT, nullptr, &now);
    return now.sa_handler == expected;
}

bool scopes() {
    constexpr std::size_t n = pygim::detail::interrupt_hub::max_scopes;
    ::signal(SIGINT, program_handler);
    std::atomic<std::size_t> entered{0}, woken{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> holders;
    for (std::size_t t = 0; t < n; ++t) {
        holders.emplace_back([&] {
            pygim::interrupt_scope scope;
            entered.fetch_add(1);
            if (scope.wait()) woken.fetch_add(1);   // only a signal wakes it: nobody calls finish()
            while (!go.load()) std::this_thread::yield();   // every one still held until all were woken
        });
    }
    while (entered.load() < n) std::this_thread::yield();
    bool refused = false;
    try {
        pygim::interrupt_scope extra;
    } catch (const std::runtime_error&) {
        refused = true;
    }
    const bool still_ours = !handler_is(program_handler);   // the 64 still own SIGINT
    ::kill(::getpid(), SIGINT);
    while (woken.load() < n) std::this_thread::yield();
    const bool not_yet_theirs = g_theirs.load() == 0;       // raised again only when the last leaves
    go.store(true);
    for (auto& h : holders) h.join();
    const bool given_back = handler_is(program_handler) && g_theirs.load() == 1;
    ::signal(SIGINT, SIG_DFL);
    const bool ok = refused && still_ours && not_yet_theirs && given_back;
    std::printf("scopes  %s\n", ok ? "64 held at once, the 65th refused, one SIGINT woke all, the program's handler back and run once"
                                   : !refused ? "A 65TH SCOPE WAS TAKEN" : !still_ours ? "THE REFUSAL GAVE THE HANDLER BACK"
                                   : !not_yet_theirs ? "THE SIGNAL REACHED THE PROGRAM WHILE SCOPES HELD IT" : "THE HANDLER WAS NOT GIVEN BACK, OR RAN NOT ONCE");
    return ok;
}

}  // namespace

// Every part, or those named on the command line.
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // each line out before an alarm's _exit
    const std::vector<std::string_view> asked(argv + 1, argv + argc);
    bool ok = true;
    auto part = [&](const char* name, unsigned seconds, bool (*run)()) {
        if (asked.empty() || std::find(asked.begin(), asked.end(), name) != asked.end()) ok &= timed_part(name, seconds, run);
    };
    part("pool", 120, pool);
    part("blocks", 120, blocks);
    part("fork", 300, fork_while_busy);
    part("ordered", 60, ordered);
    part("tasks", 60, tasks);
    part("caller", 60, caller_throws);
    part("starved", 60, starved);
    part("scopes", 60, scopes);
    std::printf("%s\n", ok ? "every part held" : "a part failed");
    return ok ? 0 : 1;
}
