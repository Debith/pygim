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

#include "../../src/_pygim_fast/platform/libraries.h"
#include "../../src/_pygim_fast/platform/memory.h"

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

}  // namespace
