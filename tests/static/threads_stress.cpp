// tests/static/threads_stress.cpp — the pool every thread of a process shares
// (core/primitives/parallel.h), driven hard under ThreadSanitizer, or AddressSanitizer with
// UndefinedBehaviorSanitizer.
//
// Built and run by run_threads_stress.py, once per sanitizer; CI's threads-stress job runs it on
// every pull request. Each part prints one line and the process exits 0 when every part held; a
// sanitizer report ends it first (-fno-sanitize-recover).
//
//   pool      for_each: every task once, workers in range, a for_each inside a task, four threads
//             calling at once, an exception rethrown by the caller every time
//   fork      a child forked while other threads run for_each, which then runs one itself: a lock
//             the fork left held, or a pool whose threads stayed behind, hangs it, and the parent
//             gives up after five seconds
//   ordered   ordered_pool: a task that throws still counts as done, so run_all's finished()
//             fires and next() rethrows at its place; run_all's window holds every result
//             whatever its byte limit; next() past the last result refuses
//   tasks     task_pool: run_all of nothing may be told from a finished() that asks for errors;
//             a task's exception reaches run_here's caller after the others ran
//   caller    parked_workers::run: when the caller's own share throws, every helper that took a
//             ticket has left the body before the exception reaches the caller
//   starved   a child that may start no thread at all: for_each runs on the caller, task_pool's
//             run_here too, and ordered_pool's run_all says it could not start
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

#include "../../src/_pygim_fast/core/primitives/parallel.h"

namespace {

using pygim::primitives::parallel::for_each;
namespace parallel = pygim::primitives::parallel;
using namespace pygim::primitives::time_literals;

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

bool fork_while_busy() {
    std::atomic<bool> stop{false};
    std::vector<std::thread> busy;
    for (unsigned t = 0; t < 4; ++t) {
        busy.emplace_back([&] {
            while (!stop.load()) for_each(32, 4, [](std::size_t, std::size_t) {});
        });
    }
    pygim::primitives::sleep_for(50_ms);
    int hung = 0, failed = 0;
    for (int k = 0; k < 20; ++k) {
        const pid_t pid = ::fork();
        if (pid == 0) {
            std::atomic<int> sum{0};
            for_each(64, 16, [&](std::size_t t, std::size_t) { sum += static_cast<int>(t); });
            ::_exit(sum == 2016 ? 0 : 1);
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
        pygim::primitives::sleep_for(50_ms);
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
    part("fork", 300, fork_while_busy);
    part("ordered", 60, ordered);
    part("tasks", 60, tasks);
    part("caller", 60, caller_throws);
    part("starved", 60, starved);
    std::printf("%s\n", ok ? "every part held" : "a part failed");
    return ok ? 0 : 1;
}
