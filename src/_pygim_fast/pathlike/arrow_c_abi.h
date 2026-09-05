#pragma once
// pathlike/arrow_c_abi.h — the Arrow C Data Interface, as an ABI only.
//
// These three structs ARE the interface: their layout is fixed by the Apache
// Arrow specification (https://arrow.apache.org/docs/format/CDataInterface.html),
// which tells producers and consumers to copy the definitions into their own
// code rather than depend on a library. pathlike does exactly that: it fills
// them from its own buffers (path_table.h columns are Arrow-layout by
// construction) and hands them to Python inside PyCapsules (the Arrow
// PyCapsule protocol: __arrow_c_schema__ / __arrow_c_array__ /
// __arrow_c_stream__), and it reads them back from any object that speaks the
// same protocol. No libarrow is linked and no Python Arrow library is called;
// pyarrow, polars and duckdb are consumers on the other side of the boundary.
//
// The include guards are the ones the specification prescribes, so this header
// coexists with Arrow's own abi.h in a translation unit that has both.

#include <cstdint>

#ifndef ARROW_C_DATA_INTERFACE
#define ARROW_C_DATA_INTERFACE

#define ARROW_FLAG_DICTIONARY_ORDERED 1
#define ARROW_FLAG_NULLABLE 2
#define ARROW_FLAG_MAP_KEYS_SORTED 4

struct ArrowSchema {
    // Array type description
    const char* format;
    const char* name;
    const char* metadata;
    int64_t flags;
    int64_t n_children;
    struct ArrowSchema** children;
    struct ArrowSchema* dictionary;

    // Release callback
    void (*release)(struct ArrowSchema*);
    // Opaque producer-specific data
    void* private_data;
};

struct ArrowArray {
    // Array data description
    int64_t length;
    int64_t null_count;
    int64_t offset;
    int64_t n_buffers;
    int64_t n_children;
    const void** buffers;
    struct ArrowArray** children;
    struct ArrowArray* dictionary;

    // Release callback
    void (*release)(struct ArrowArray*);
    // Opaque producer-specific data
    void* private_data;
};

#endif  // ARROW_C_DATA_INTERFACE

#ifndef ARROW_C_STREAM_INTERFACE
#define ARROW_C_STREAM_INTERFACE

struct ArrowArrayStream {
    // Callbacks providing stream functionality
    int (*get_schema)(struct ArrowArrayStream*, struct ArrowSchema* out);
    int (*get_next)(struct ArrowArrayStream*, struct ArrowArray* out);
    const char* (*get_last_error)(struct ArrowArrayStream*);

    // Release callback
    void (*release)(struct ArrowArrayStream*);
    // Opaque producer-specific data
    void* private_data;
};

#endif  // ARROW_C_STREAM_INTERFACE
