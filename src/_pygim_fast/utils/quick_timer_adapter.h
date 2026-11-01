#pragma once
// utils/quick_timer_adapter.h — QuickTimer, Report and TimerSnapshot for Python.
//
// Translation only: Python chooses the clock and the report at run time, so the adapter holds
// each as a variant chosen by argument (python_clock: the monotonic clock or a ManualTime;
// python_report: silent, text on sys.stderr, hooks calling Python, or several at once) and hands the core
// timer one clock_source and one timer_report. Lap names the timer keeps as views are Python
// strings this adapter keeps alive; a snapshot handed to Python copies them.
//
//     with utils.QuickTimer.silent("load csv", clock=time) as timer:
//         with timer.lap("read"):
//             ...
//     timer.laps                  -> {"read": Duration(...)}
//     timer.total.seconds         -> 0.002

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <pybind11/pybind11.h>

#include "duration.h"
#include "quick_timer.h"
#include "timestamp.h"

namespace pygim::adapter {

namespace py = pybind11;

/// The clock a Python QuickTimer reads: the monotonic clock, or a ManualTime it was given.
class python_clock {
public:
    python_clock() = default;
    explicit python_clock(const manual_time& time) : m_source(time.clock()) {}
    [[nodiscard]] std::int64_t now_ns() const {
        return std::visit([](const auto& clock) { return clock.now_ns(); }, m_source);
    }

private:
    std::variant<monotonic_clock, manual_clock> m_source;
};
static_assert(clock_source<python_clock>);

/// What a Python report or hook receives: the name, the total and the laps, copied out of the timer.
class python_snapshot {
public:
    explicit python_snapshot(const timer_snapshot& s) : m_name(s.name()), m_total(s.total()) {
        for (const lap_time& lap : s.laps()) m_laps.emplace_back(std::string(lap.name()), lap.took());
    }
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    [[nodiscard]] duration total() const noexcept { return m_total; }
    /// The laps as a dict, in the order they first started.
    [[nodiscard]] py::dict laps() const {
        py::dict out;
        for (const auto& [name, took] : m_laps) out[py::str(name)] = took;
        return out;
    }

private:
    std::string m_name;
    duration m_total;
    std::vector<std::pair<std::string, duration>> m_laps;
};

class python_report;
using report_parts = std::vector<std::shared_ptr<const python_report>>;   // a Report.both()'s reports, in order

/// Python's sys.stderr, what contextlib.redirect_stderr, capsys and a notebook see: the text
/// report's sink (the GIL is held: a timer's moments happen in Python's calls). No sys.stderr, nothing.
class python_stderr {
public:
    [[nodiscard]] bool open() const noexcept { return true; }
    void write(std::string_view text) const {
        PyObject* err = PySys_GetObject("stderr");   // borrowed
        if (err == nullptr || err == Py_None) return;
        if (PyFile_WriteString(std::string(text).c_str(), err) != 0) throw py::error_already_set();
    }
};
static_assert(text_sink<python_stderr>);

/// Several reports at once: each moment goes to each, in the order they were given.
class several_reports {
public:
    explicit several_reports(report_parts parts) : m_parts(std::move(parts)) {}

    template <one_of<timer_started, lap_started, lap_stopped, timer_finished> E, class... Args>
    void run(const Args&... args) const;
    template <one_of<timer_started, lap_started, lap_stopped, timer_finished> E>
    [[nodiscard]] bool has() const;

private:
    report_parts m_parts;
};

/// Python's Report: one of silent, text on sys.stderr, hooks calling Python callables, or several reports.
class python_report {
public:
    static python_report silent_report() { return python_report(silent{}); }
    static python_report stderr_text(bool started, bool stopped, bool summary) {
        report_lines lines = report_lines::none;
        if (started) lines = lines | report_lines::started;
        if (stopped) lines = lines | report_lines::stopped;
        if (summary) lines = lines | report_lines::summary;
        return python_report(text_report<python_stderr>(lines));
    }
    static python_report hooks_report() { return python_report(timer_hooks{}); }
    static python_report all_of(report_parts parts) {
        return python_report(several_reports(std::move(parts)));
    }

    template <one_of<timer_started, lap_started, lap_stopped, timer_finished> E, class... Args>
    void run(const Args&... args) const {
        std::visit([&](const auto& kind) { kind.template run<E>(args...); }, m_kind);
    }
    template <one_of<timer_started, lap_started, lap_stopped, timer_finished> E>
    [[nodiscard]] bool has() const {
        return std::visit([](const auto& kind) { return static_cast<bool>(kind.template has<E>()); }, m_kind);
    }

    /// Adds a Python callable to the hooks of `E`; a TypeError on a report that is not Report.hooks().
    template <one_of<timer_started, lap_started, lap_stopped, timer_finished> E>
    void add_hook(py::function callback) {
        auto* callbacks = std::get_if<timer_hooks>(&m_kind);
        if (callbacks == nullptr) throw py::type_error("only a Report.hooks() report takes hooks");
        callbacks->add<E>(calling(E{}, std::move(callback)));
    }

private:
    std::variant<silent, text_report<python_stderr>, timer_hooks, several_reports> m_kind;

    template <class Kind>
    explicit python_report(Kind kind) : m_kind(std::move(kind)) {}

    // Each event's C++ arguments, as the Python callable is given them.
    static auto calling(timer_started, py::function f) {
        return [f = std::move(f)](std::string_view timer) { f(py::str(timer.data(), timer.size())); };
    }
    static auto calling(lap_started, py::function f) {
        return [f = std::move(f)](std::string_view timer, std::string_view lap) {
            f(py::str(timer.data(), timer.size()), py::str(lap.data(), lap.size()));
        };
    }
    static auto calling(lap_stopped, py::function f) {
        return [f = std::move(f)](std::string_view timer, std::string_view lap, duration took) {
            f(py::str(timer.data(), timer.size()), py::str(lap.data(), lap.size()), took);
        };
    }
    static auto calling(timer_finished, py::function f) {
        return [f = std::move(f)](const timer_snapshot& s) { f(python_snapshot(s)); };
    }
};
static_assert(timer_report<python_report>);

template <one_of<timer_started, lap_started, lap_stopped, timer_finished> E, class... Args>
void several_reports::run(const Args&... args) const {
    for (const auto& part : m_parts) part->template run<E>(args...);
}
template <one_of<timer_started, lap_started, lap_stopped, timer_finished> E>
bool several_reports::has() const {
    for (const auto& part : m_parts) {
        if (part->template has<E>()) return true;
    }
    return false;
}

/// The report a Python timer holds: a shared handle to the Report object, so hooks added later count.
class python_report_handle {
public:
    explicit python_report_handle(std::shared_ptr<const python_report> report) : m_report(std::move(report)) {}

    template <one_of<timer_started, lap_started, lap_stopped, timer_finished> E, class... Args>
    void run(const Args&... args) const { m_report->template run<E>(args...); }
    template <one_of<timer_started, lap_started, lap_stopped, timer_finished> E>
    [[nodiscard]] bool has() const { return m_report->template has<E>(); }

private:
    std::shared_ptr<const python_report> m_report;
};
static_assert(timer_report<python_report_handle>);

/// Python's QuickTimer: the core timer over python_clock and python_report_handle, with the
/// Python strings and the ManualTime its views point into kept alive.
class python_quick_timer {
public:
    using core_timer = QuickTimerT<void, 8, python_clock, python_report_handle>;

    python_quick_timer(py::str name, py::object clock, const py::object& report)
        : m_name(std::move(name)),
          m_clock_owner(std::move(clock)),
          m_timer(view_of(m_name), clock_of(m_clock_owner), report_of(report)) {}

    /// Starts the lap `name`. A new name's string is kept before the timer may store its view, and
    /// let go again when the timer did not store it (it had finished, or had no room).
    void start_lap(const py::str& name) {
        if (const std::optional<std::string_view> known = kept_view_of(view_of(name))) return m_timer.start_lap(*known);
        m_lap_names.push_back(name);
        try {
            m_timer.start_lap(view_of(m_lap_names.back()));
        } catch (...) {
            if (m_timer.lap_count() < m_lap_names.size()) m_lap_names.pop_back();   // not stored: no view of it remains
            throw;
        }
    }
    duration stop_lap(const std::optional<py::str>& name) {
        return name ? m_timer.stop_lap(view_of(*name)) : m_timer.stop_lap();
    }
    void finish() { m_timer.finish(); }
    [[nodiscard]] python_snapshot snapshot() const { return python_snapshot(m_timer.snapshot()); }
    /// From the start to now, or to the finish once finished.
    [[nodiscard]] duration total() const { return m_timer.total(); }
    /// All the time of the lap `name`; IndexError when there is no such lap.
    [[nodiscard]] duration lap_time(const py::str& name) const { return m_timer.lap(view_of(name)); }

private:
    py::str m_name;
    py::object m_clock_owner;
    std::vector<py::str> m_lap_names;   // the strings the timer's lap names view, one per lap
    core_timer m_timer;                 // last: destroyed first, while the strings it views live

    [[nodiscard]] static std::string_view view_of(const py::str& s) {
        Py_ssize_t size = 0;
        const char* data = PyUnicode_AsUTF8AndSize(s.ptr(), &size);
        if (data == nullptr) throw py::error_already_set();
        return {data, static_cast<std::size_t>(size)};
    }
    [[nodiscard]] static python_clock clock_of(const py::object& clock) {
        if (clock.is_none()) return python_clock();
        if (!py::isinstance<manual_time>(clock)) throw py::type_error("clock must be a ManualTime, or None for the monotonic clock");
        return python_clock(py::cast<const manual_time&>(clock));
    }
    [[nodiscard]] static python_report_handle report_of(const py::object& report) {
        if (report.is_none()) return python_report_handle(std::make_shared<python_report>(python_report::stderr_text(true, true, true)));
        if (!py::isinstance<python_report>(report)) throw py::type_error("report must be a Report, or None for Report.stderr()");
        return python_report_handle(py::cast<std::shared_ptr<python_report>>(report));
    }
    // The kept string's view of a lap already named `text`, if one is.
    [[nodiscard]] std::optional<std::string_view> kept_view_of(std::string_view text) const {
        for (const py::str& known : m_lap_names) {
            if (view_of(known) == text) return view_of(known);
        }
        return std::nullopt;
    }
};

/// f() as a with block ends. A hook that raises then raises here; the block's own error, if any,
/// becomes its __context__, as a Python __exit__'s would — pybind's re-raise left it out, and it was lost.
template <class F>
void ending_block(const py::object& error, F&& f) {
    try {
        f();
    } catch (py::error_already_set& raised) {
        if (!error.is_none()) PyException_SetContext(raised.value().ptr(), error.inc_ref().ptr());   // steals the reference
        throw;
    }
}

/// `with timer.lap("read") as lap:` — the lap started as the block begins and stopped as it ends;
/// `lap.took` is all its time so far. It holds its timer, so it never outlives what it reads.
class python_timer_lap {
public:
    python_timer_lap(py::object timer, py::str name) : m_timer(std::move(timer)), m_name(std::move(name)) {}

    void start() { timer().start_lap(m_name); }
    void stop() { timer().stop_lap(m_name); }
    [[nodiscard]] const py::str& name() const noexcept { return m_name; }
    [[nodiscard]] duration took() const { return timer().lap_time(m_name); }

private:
    py::object m_timer;   // a QuickTimer
    py::str m_name;

    [[nodiscard]] python_quick_timer& timer() const { return m_timer.cast<python_quick_timer&>(); }
};

/// Binds TimerSnapshot, TimerLap, Report and QuickTimer into `m` (Duration and ManualTime must be bound first).
inline void bind_quick_timer(py::module_& m) {
    py::class_<python_snapshot>(m, "TimerSnapshot", "What a QuickTimer measured: its name, total and laps.")
        .def_property_readonly("name", &python_snapshot::name, "The timer's name.")
        .def_property_readonly("total", &python_snapshot::total, "From the timer's start to its finish (or now).")
        .def_property_readonly("laps", &python_snapshot::laps, "{lap name: Duration}, in the order the laps first started.")
        .def("__repr__", [](const python_snapshot& s) {
            return "TimerSnapshot(" + py::repr(py::str(s.name())).cast<std::string>() +
                   ", total=" + py::repr(py::cast(s.total())).cast<std::string>() + ")";
        });

    py::class_<python_timer_lap>(m, "TimerLap", "A lap of a QuickTimer as a with block: QuickTimer.lap(name).")
        .def_property_readonly("name", &python_timer_lap::name, "The lap's name.")
        .def_property_readonly("took", &python_timer_lap::took, "All the lap's time so far: every block it ran.")
        .def("__enter__", [](py::object self) {
            self.cast<python_timer_lap&>().start();
            return self;
        })
        .def("__exit__", [](python_timer_lap& lap, const py::object&, const py::object& error, const py::object&) {
            ending_block(error, [&] { lap.stop(); });
            return false;
        }, py::arg("exc_type"), py::arg("exc"), py::arg("traceback"));

    py::class_<python_report, std::shared_ptr<python_report>>(m, "Report",
        "How a QuickTimer tells what it measured: Report.silent(), Report.stderr(), Report.hooks(), Report.both(...).")
        .def_static("silent", [] { return std::make_shared<python_report>(python_report::silent_report()); },
                    "Tells nobody.")
        .def_static("stderr", [](bool started, bool stopped, bool summary) {
                        return std::make_shared<python_report>(python_report::stderr_text(started, stopped, summary));
                    },
                    py::kw_only(), py::arg("started") = true, py::arg("stopped") = true, py::arg("summary") = true,
                    "Text on sys.stderr, as QuickTimer has always printed it; each kind of line on or off.")
        .def_static("hooks", [] { return std::make_shared<python_report>(python_report::hooks_report()); },
                    "Your callbacks: on_timer_started, on_started, on_stopped, on_finished.")
        .def_static("both", [](const py::args& parts) {
                        report_parts all;
                        for (const py::handle part : parts) {
                            if (!py::isinstance<python_report>(part)) throw py::type_error("Report.both takes Reports only");
                            all.push_back(py::cast<std::shared_ptr<python_report>>(part));
                        }
                        return std::make_shared<python_report>(python_report::all_of(std::move(all)));
                    },
                    "Several reports at once, each hearing every moment in turn.")
        .def("on_timer_started", &python_report::add_hook<timer_started>, py::arg("callback"),
             "callback(timer_name) when the timer starts.")
        .def("on_started", &python_report::add_hook<lap_started>, py::arg("callback"),
             "callback(timer_name, lap) when a lap starts.")
        .def("on_stopped", &python_report::add_hook<lap_stopped>, py::arg("callback"),
             "callback(timer_name, lap, took) when a lap stops: took is a Duration, all the time that lap has run.")
        .def("on_finished", &python_report::add_hook<timer_finished>, py::arg("callback"),
             "callback(snapshot) when the timer finishes.");

    py::class_<python_quick_timer>(m, "QuickTimer",
        "Measures a block and the laps inside it. clock: where 'now' comes from (the monotonic clock unless a\n"
        "ManualTime is given). report: how it tells what it measured (text on sys.stderr unless given).\n"
        "Use it in a with block: leaving it finishes the timer.")
        .def(py::init<py::str, py::object, const py::object&>(), py::arg("name") = py::str("QuickTimer"), py::kw_only(),
             py::arg("clock") = py::none(), py::arg("report") = py::none())
        .def("start_lap", &python_quick_timer::start_lap, py::arg("name"),
             "Starts the lap `name`, stopping the one running.")
        .def("stop_lap", &python_quick_timer::stop_lap, py::arg("name") = py::none(),
             "Stops the lap `name` (the running one when None) and returns all its time.")
        .def("finish", &python_quick_timer::finish, "Stops the running lap, freezes the total, tells the report: once.")
        .def_static("silent", [](py::str name, py::object clock) {
                return std::make_unique<python_quick_timer>(std::move(name), std::move(clock),
                                                            py::cast(std::make_shared<python_report>(python_report::silent_report())));
            }, py::arg("name") = py::str("QuickTimer"), py::kw_only(), py::arg("clock") = py::none(),
            "A timer that tells nobody: QuickTimer(name, clock=clock, report=Report.silent()).")
        .def("snapshot", &python_quick_timer::snapshot, "What it measured so far, or at its finish.")
        .def_property_readonly("total", &python_quick_timer::total, "From the start to now, or to the finish once finished.")
        .def_property_readonly("laps", [](const python_quick_timer& timer) { return timer.snapshot().laps(); },
                               "{lap name: Duration}, in the order the laps first started.")
        .def("lap", [](py::object self, py::str name) { return python_timer_lap(std::move(self), std::move(name)); },
             py::arg("name"), "The lap `name` as a with block: started as the block begins, stopped as it ends.")
        .def("__enter__", [](py::object self) { return self; })
        .def("__exit__", [](python_quick_timer& timer, const py::object&, const py::object& error, const py::object&) {
            ending_block(error, [&] { timer.finish(); });
            return false;
        }, py::arg("exc_type"), py::arg("exc"), py::arg("traceback"));
}

}  // namespace pygim::adapter
