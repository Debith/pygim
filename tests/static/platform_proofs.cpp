// Compile-time proofs for the platform boundary's logic over fake system calls (D138): each
// system's class is a template on the calls it makes, so the logic that decides which file to
// open, what a reading means and what a failure answers is proven here on every system, with the
// calls answered by constexpr fakes. The calls themselves are proven by CI on their own system;
// the join of the two, once per system, by the suite there.
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
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace pygim::platform;

#define CHECK(cond)                 \
    do {                            \
        ++step;                     \
        if (!(cond)) return step;   \
    } while (false)

// ── a POSIX loader over fake dlopen, dlsym and glob ───────────────────────────────────────────

constexpr void a_function() {}
constexpr void b_function() {}

/// Two libraries are installed: 1 answers to its soname and to the 18 file, 2 to the 17 file.
struct fake_dl_calls {
    using handle = int;
    static constexpr handle none = 0;
    [[nodiscard]] static constexpr handle dlopen_now(std::string_view name) noexcept {
        if (name == "libodbc.so.2" || name == "/opt/driver/lib/libmsodbcsql-18.so") return 1;
        if (name == "/opt/driver/lib/libmsodbcsql-17.so") return 2;
        return none;
    }
    [[nodiscard]] static constexpr symbol_address dlsym_of(handle h, std::string_view name) noexcept {
        if (h == 1 && name == "a") return &a_function;
        if (h == 2 && name == "b") return &b_function;
        return nullptr;
    }
    [[nodiscard]] static constexpr std::vector<std::string> glob_sorted(std::string_view pattern) {
        if (pattern == "/opt/driver/lib/libmsodbcsql-*.so") return {"/opt/driver/lib/libmsodbcsql-17.so", "/opt/driver/lib/libmsodbcsql-18.so"};
        return {};
    }
};
static_assert(posix_library_calls<fake_dl_calls>);

using fake_posix_loader = basic_posix_loader<fake_dl_calls>;
static_assert(library_loader<fake_posix_loader>);

constexpr int posix_loading_holds() {
    int step = 0;
    using lib = basic_library<fake_posix_loader>;
    constexpr const char* names[] = {"libnothere.so", "libodbc.so.2", "/opt/driver/lib/libmsodbcsql-17.so"};
    const lib first = lib::first_of(names);
    CHECK(first && first.address_of("a") == &a_function && first.address_of("zzz") == nullptr);   // the first installed name, not the last
    constexpr const char* none[] = {"libnothere.so", "libneither.so"};
    CHECK(!lib::first_of(none) && !lib::first_of(none).address_of("a"));                         // nothing installed: no library, no address
    CHECK(!lib::first_of({}));
    const lib newest = lib::last_match("/opt/driver/lib/libmsodbcsql-*.so");
    CHECK(newest && newest.address_of("a") == &a_function);                                       // the last of glob's sorted matches: the 18 file
    CHECK(!lib::last_match("/opt/none/*.so"));                                                     // no match: none
    CHECK(fake_posix_loader::symbol(2, "b") == &b_function && fake_posix_loader::symbol(2, "a") == nullptr);
    return 0;
}
static_assert(posix_loading_holds() == 0);

// ── a Windows loader over fake LoadLibraryW and GetProcAddress ────────────────────────────────

/// One library is installed, found by its wide name.
struct fake_win32_calls {
    using handle = int;
    static constexpr handle none = 0;
    [[nodiscard]] static constexpr handle load_library(const wchar_t* name) noexcept {
        return std::wstring_view(name) == L"msodbcsql18.dll" ? 7 : none;
    }
    [[nodiscard]] static constexpr symbol_address proc_address(handle h, std::string_view name) noexcept {
        return h == 7 && name == "bcp_init" ? &a_function : nullptr;
    }
};
static_assert(windows_library_calls<fake_win32_calls>);

using fake_windows_loader = basic_windows_loader<fake_win32_calls>;
static_assert(library_loader<fake_windows_loader>);

constexpr int windows_loading_holds() {
    int step = 0;
    using lib = basic_library<fake_windows_loader>;
    constexpr const char* names[] = {"msodbcsql17.dll", "msodbcsql18.dll"};
    const lib found = lib::first_of(names);
    CHECK(found && found.address_of("bcp_init") == &a_function && found.address_of("other") == nullptr);   // the narrow name widened, char for char
    CHECK(!lib::last_match("C:\\drivers\\*.dll"));                                                         // Windows has no glob: never a match
    CHECK(fake_windows_loader::open("") == fake_win32_calls::none);
    return 0;
}
static_assert(windows_loading_holds() == 0);

// ── the memory probes over fake readings ──────────────────────────────────────────────────────

/// Linux: a statm line of 1000 pages held and 250 resident, 4 KiB pages, and a peak the kernel
/// reports in kilobytes; `peak_kb` below the present reading proves the larger wins.
template <std::uint64_t peak_kb, bool procfs = true>
struct fake_proc_calls {
    [[nodiscard]] static constexpr std::size_t read_statm(char* buf, std::size_t n) noexcept {
        if (!procfs) return 0;
        constexpr std::string_view line = "1000 250 60 10 0 500 0\n";
        const std::size_t count = line.size() < n ? line.size() : n;
        for (std::size_t i = 0; i < count; ++i) buf[i] = line[i];
        return count;
    }
    [[nodiscard]] static constexpr std::uint64_t page_size() noexcept { return 4096; }
    [[nodiscard]] static constexpr std::optional<std::uint64_t> max_rss() noexcept {
        return peak_kb == 0 ? std::nullopt : std::optional<std::uint64_t>{peak_kb};
    }
};
static_assert(linux_memory_calls<fake_proc_calls<2000>>);
static_assert(process_memory<basic_linux_memory<fake_proc_calls<2000>>>);

constexpr int linux_memory_holds() {
    int step = 0;
    using high = basic_linux_memory<fake_proc_calls<2000>>;
    CHECK(high::resident_bytes() == 250u * 4096u);                   // the second field, in pages, times the page size
    CHECK(high::peak_resident_bytes() == 2000u * 1024u);             // ru_maxrss is kilobytes on Linux
    using low = basic_linux_memory<fake_proc_calls<100>>;
    CHECK(low::peak_resident_bytes() == 250u * 4096u);               // a peak the kernel has not caught up with: the present reading wins
    using unsampled = basic_linux_memory<fake_proc_calls<0>>;
    CHECK(unsampled::peak_resident_bytes() == 250u * 4096u);         // getrusage failed: the present reading
    using no_procfs = basic_linux_memory<fake_proc_calls<2000, false>>;
    CHECK(no_procfs::resident_bytes() == 0 && no_procfs::peak_resident_bytes() == 2000u * 1024u);   // no /proc: 0, and the peak alone
    return 0;
}
static_assert(linux_memory_holds() == 0);

/// macOS: the Mach task's resident size, and a peak the kernel reports in bytes.
template <bool answers>
struct fake_mach_calls {
    [[nodiscard]] static constexpr std::optional<std::uint64_t> task_resident_size() noexcept {
        return answers ? std::optional<std::uint64_t>{123'456} : std::nullopt;
    }
    [[nodiscard]] static constexpr std::optional<std::uint64_t> max_rss() noexcept {
        return answers ? std::optional<std::uint64_t>{654'321} : std::nullopt;
    }
};
static_assert(macos_memory_calls<fake_mach_calls<true>>);
static_assert(process_memory<basic_macos_memory<fake_mach_calls<true>>>);
static_assert(basic_macos_memory<fake_mach_calls<true>>::resident_bytes() == 123'456);          // bytes as the task reports them
static_assert(basic_macos_memory<fake_mach_calls<true>>::peak_resident_bytes() == 654'321);     // ru_maxrss is bytes on macOS
static_assert(basic_macos_memory<fake_mach_calls<false>>::resident_bytes() == 0 &&
              basic_macos_memory<fake_mach_calls<false>>::peak_resident_bytes() == 0);          // a failed call reads 0, never throws

/// Windows: the working set and its peak, in one call that may fail.
template <bool answers>
struct fake_psapi_calls {
    [[nodiscard]] static constexpr std::optional<std::pair<std::uint64_t, std::uint64_t>> working_set() noexcept {
        return answers ? std::optional<std::pair<std::uint64_t, std::uint64_t>>{{5'000, 7'000}} : std::nullopt;
    }
};
static_assert(windows_memory_calls<fake_psapi_calls<true>>);
static_assert(process_memory<basic_windows_memory<fake_psapi_calls<true>>>);
static_assert(basic_windows_memory<fake_psapi_calls<true>>::resident_bytes() == 5'000 &&
              basic_windows_memory<fake_psapi_calls<true>>::peak_resident_bytes() == 7'000);
static_assert(basic_windows_memory<fake_psapi_calls<false>>::resident_bytes() == 0 &&
              basic_windows_memory<fake_psapi_calls<false>>::peak_resident_bytes() == 0);

// The running system's own classes satisfy the same contracts: the fakes prove the logic, these
// prove the shape the real calls take.
static_assert(library_loader<native_loader>);
static_assert(process_memory<native_memory>);

}  // namespace
