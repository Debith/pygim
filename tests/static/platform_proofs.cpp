// Compile-time proofs for the logic the platform boundary puts above the system's calls (D138): a
// class with such logic is a template on its calls, so the logic is proven here on every system,
// with the calls answered by constexpr fakes. The calls themselves are proven by CI on their own
// system; the join of the two, once per system, by the suite there.
//
// This TU is part of the pygim.utils extension's SOURCES (ext.utils.toml): it is compiled by every
// build, so a violated rule cannot produce a binary. It contributes no runtime code.
//
// Every proof is a constexpr function that returns the number of the first step that failed, or 0,
// so a failing assertion names the step in the compiler's "comparison reduces to" note.
//
// A component with no logic above its calls, the file lock (platform/locks.h), is included only so
// that every build compiles the running system's calls against their concept.

#include "../../src/_pygim_fast/platform/caches.h"
#include "../../src/_pygim_fast/platform/libraries.h"
#include "../../src/_pygim_fast/platform/locks.h"
#include "../../src/_pygim_fast/platform/memory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace pygim::platform;

#define CHECK(cond)                 \
    do {                            \
        ++step;                     \
        if (!(cond)) return step;   \
    } while (false)

// A fake's handles are the addresses of its own objects, as the system's are of its own.
int odbc_2 = 0, driver_17 = 0, driver_18 = 0;
constexpr void a_function() {}
constexpr void b_function() {}

// ── POSIX: the first installed name, the newest of glob's matches ───────────────────────────────

/// Installed: libodbc.so.2 and the 18 file, which answer for "a", and the 17 file, which answers for "b".
struct fake_dl_calls {
    [[nodiscard]] static constexpr void* dlopen_now(std::string_view name) noexcept {
        if (name == "libodbc.so.2") return &odbc_2;
        if (name == "/opt/driver/lib/libmsodbcsql-17.so") return &driver_17;
        if (name == "/opt/driver/lib/libmsodbcsql-18.so") return &driver_18;
        return nullptr;
    }
    [[nodiscard]] static constexpr symbol_address dlsym_of(void* h, std::string_view name) noexcept {
        if ((h == &odbc_2 || h == &driver_18) && name == "a") return &a_function;
        if (h == &driver_17 && name == "b") return &b_function;
        return nullptr;
    }
    [[nodiscard]] static constexpr std::vector<std::string> glob_sorted(std::string_view pattern) noexcept {
        if (pattern == "/opt/driver/lib/libmsodbcsql-*.so") return {"/opt/driver/lib/libmsodbcsql-17.so", "/opt/driver/lib/libmsodbcsql-18.so"};
        return {};
    }
};
using fake_posix_loader = basic_posix_loader<fake_dl_calls>;
static_assert(library_loader<fake_posix_loader>);

constexpr int posix_loading_holds() {
    int step = 0;
    using lib = basic_library<fake_posix_loader>;
    constexpr const char* names[] = {"libnothere.so", "/opt/driver/lib/libmsodbcsql-17.so", "libodbc.so.2"};
    const lib first = lib::first_of(names);
    CHECK(first && first.address_of("b") == &b_function && first.address_of("a") == nullptr);   // the first name that opens, not a later one
    constexpr const char* none[] = {"libnothere.so", "libneither.so"};
    CHECK(!lib::first_of(none) && lib::first_of(none).address_of("a") == nullptr);             // nothing installed: no library, no address
    CHECK(!lib::first_of({}));
    const lib newest = lib::last_match("/opt/driver/lib/libmsodbcsql-*.so");
    CHECK(newest && newest.address_of("a") == &a_function && newest.address_of("b") == nullptr);   // the last of glob's sorted matches: the 18 file
    CHECK(!lib::last_match("/opt/none/*.so"));                                                   // no match: none
    return 0;
}
static_assert(posix_loading_holds() == 0);

// ── Windows: a name widened, no glob ──────────────────────────────────────────────────────────

/// Installed: msodbcsql18.dll, found by its wide name.
struct fake_win32_calls {
    [[nodiscard]] static constexpr void* load_library(const wchar_t* name) noexcept {
        return std::wstring_view(name) == L"msodbcsql18.dll" ? &driver_18 : nullptr;
    }
    [[nodiscard]] static constexpr symbol_address proc_address(void* h, std::string_view name) noexcept {
        return h == &driver_18 && name == "bcp_init" ? &a_function : nullptr;
    }
};
using fake_windows_loader = basic_windows_loader<fake_win32_calls>;
static_assert(library_loader<fake_windows_loader>);

constexpr int windows_loading_holds() {
    int step = 0;
    using lib = basic_library<fake_windows_loader>;
    constexpr const char* names[] = {"msodbcsql17.dll", "msodbcsql18.dll"};
    const lib found = lib::first_of(names);
    CHECK(found && found.address_of("bcp_init") == &a_function && found.address_of("other") == nullptr);   // the narrow name widened, char for char
    CHECK(!lib::last_match("msodbcsql18.dll"));                                                            // no glob: never a match, even for a name that is installed
    CHECK(fake_windows_loader::open("") == nullptr);
    return 0;
}
static_assert(windows_loading_holds() == 0);

// ── Linux's memory probes: statm's second field, the kilobyte peak, the larger of peak and present ──

/// A statm line of 1000 pages held and 250 resident, 4 KiB pages, a peak of `peak_kb` (0: getrusage
/// failed); `procfs` false: no /proc, so no line.
template <std::uint64_t peak_kb, bool procfs = true>
struct fake_proc_calls {
    [[nodiscard]] static constexpr std::size_t read_statm(char* buf, std::size_t n) noexcept {
        constexpr std::string_view line = "1000 250 60 10 0 500 0\n";
        if (!procfs || n < line.size()) return 0;
        line.copy(buf, line.size());
        return line.size();
    }
    [[nodiscard]] static constexpr std::uint64_t page_size() noexcept { return 4096; }
    [[nodiscard]] static constexpr std::uint64_t max_rss_kb() noexcept { return peak_kb; }
};
static_assert(process_memory<basic_linux_memory<fake_proc_calls<2000>>>);

constexpr int linux_memory_holds() {
    int step = 0;
    using high = basic_linux_memory<fake_proc_calls<2000>>;
    CHECK(high::resident_bytes() == 250u * 4096u);                    // the second field, in pages, times the page size
    CHECK(high::peak_resident_bytes() == 2000u * 1024u);              // ru_maxrss is kilobytes on Linux
    using low = basic_linux_memory<fake_proc_calls<100>>;
    CHECK(low::peak_resident_bytes() == 250u * 4096u);                // a peak the kernel has not caught up with: the present wins
    using unsampled = basic_linux_memory<fake_proc_calls<0>>;
    CHECK(unsampled::peak_resident_bytes() == 250u * 4096u);          // getrusage failed: the present reading
    using no_procfs = basic_linux_memory<fake_proc_calls<2000, false>>;
    CHECK(no_procfs::resident_bytes() == 0 && no_procfs::peak_resident_bytes() == 2000u * 1024u);   // no /proc: 0, and the peak alone
    return 0;
}
static_assert(linux_memory_holds() == 0);

// ── Linux's caches: sysconf first, the kernel's list where it reports no L1d or L2 ──

/// sysconf's report, and cpu0's list as the kernel writes it: a Zen 3 core, whose L1 instruction cache
/// is larger than its L1d. `conf_sizes` false: sysconf reports no size (glibc on arm64, musl), only
/// `conf_line`; `listed` false: no /sys either.
template <bool conf_sizes, std::size_t conf_line = 64, bool listed = true>
struct fake_cache_calls {
    [[nodiscard]] static constexpr reported_caches sysconf_caches() noexcept {
        return conf_sizes ? reported_caches{49152, 1310720, 25165824, conf_line} : reported_caches{0, 0, 0, conf_line};
    }
    [[nodiscard]] static constexpr std::string_view sys_cache_text(int index, const char* file, std::array<char, 32>& buffer) noexcept {
        constexpr std::array<std::array<std::string_view, 4>, 4> cpu0{{   // level, type, size, coherency_line_size
            {"1\n", "Data\n", "32K\n", "64\n"},
            {"1\n", "Instruction\n", "64K\n", "64\n"},
            {"2\n", "Unified\n", "512K\n", "64\n"},
            {"3\n", "Unified\n", "32768K\n", "64\n"}}};
        if (!listed || index >= static_cast<int>(cpu0.size())) return {};
        const std::string_view name(file);
        const std::size_t column = name == "level" ? 0 : name == "type" ? 1 : name == "size" ? 2 : 3;
        const std::string_view text = cpu0[static_cast<std::size_t>(index)][column];
        text.copy(buffer.data(), text.size());
        return {buffer.data(), text.size()};
    }
};
static_assert(cache_reports<basic_linux_caches<fake_cache_calls<true>>>);

constexpr int linux_caches_hold() {
    int step = 0;
    using sysconf_says = basic_linux_caches<fake_cache_calls<true>>;
    CHECK((sysconf_says::system_caches() == reported_caches{49152, 1310720, 25165824, 64}));   // sysconf's, as it says
    using listed = basic_linux_caches<fake_cache_calls<false>>;
    CHECK((listed::system_caches() == reported_caches{32768, 524288, 33554432, 64}));   // the list's data caches: not its larger L1i
    using no_conf_line = basic_linux_caches<fake_cache_calls<false, 0>>;
    CHECK(no_conf_line::system_caches().line() == 64);      // no line from sysconf: the list's
    using conf_line = basic_linux_caches<fake_cache_calls<false, 128>>;
    CHECK(conf_line::system_caches().line() == 128);        // sysconf's line kept over the list's
    using nothing = basic_linux_caches<fake_cache_calls<false, 0, false>>;
    CHECK(nothing::system_caches() == reported_caches());   // neither says: nothing, never a throw
    return 0;
}
static_assert(linux_caches_hold() == 0);

// ── The file lock: the running system's calls, open, lock, unlock and close over an int descriptor ──
static_assert(file_lock_calls<native_lock_calls>);

}  // namespace
