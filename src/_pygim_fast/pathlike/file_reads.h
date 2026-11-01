#pragma once
// pathlike/file_reads.h — reading one file whole: its values (read_result.h) and the running
// system's reader, native_reads (platform/reads.h).
//
//     read_result r = native_reads{}.read(path);          // a std::string, or a path_list's entry
//     std::get<arena_string>(r)         -> the bytes        not_a_file / read_failure -> why not
//     native_reads{}.read(path, &arena)                   // the bytes in a chunk_arena's chunks
//
// CORE layer: pybind-free.

#include "../platform/reads.h"
#include "read_result.h"

namespace pygim::pathlike {

using platform::native_reads;

}  // namespace pygim::pathlike
