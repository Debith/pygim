# BCP Performance Bottleneck Analysis (mssql_strategy_bcp.cpp)

Date: 2026-02-23
Branch: `core/repository`
Scope: `bulk_insert_arrow_bcp` function in `mssql_strategy_bcp.cpp`
Test Run: 1M rows, batch_size=50,000, arrow_c_stream_exporter mode, throughput=23.07 MB/s

## Executive Summary

Based on the stress test output and the source code of `mssql_strategy_bcp.cpp`, the dominant bottlenecks in the BCP implementation are excessive ODBC API calls in the inner row loop (54.4% of DB write time) and expensive batch flushes (41.9%). Current throughput (23.07 MB/s) remains below the historical target (63.63 MB/s). The sheer volume of per-cell ODBC calls (`bcp_colptr`, `bcp_collen`) and eager memory allocations for strings and temporal data add significant CPU and memory pressure.

## Detailed Findings

### 1. Excessive ODBC API Calls in the Inner Loop (`row_loop`) - **CRITICAL**
- **Evidence**: The `row_loop` phase consumes **54.4%** (2.419s) of the total DB write time.
- **Root Cause**: For every single cell (row × column), the code makes one or more ODBC API calls:
  - **Fixed-width columns:** It calls `bcp_colptr` for *every* row to update the pointer to the current row's data in the Arrow array. If the column has nulls, it also calls `bcp_collen` to toggle between `SQL_NULL_DATA` and the actual length.
  - **String columns:** It calls both `bcp_collen` (to update the length) and `bcp_colptr` (to point to the string's memory) for *every* row.
  For 1,000,000 rows and 11 columns, this results in at least **11,000,000 calls to `bcp_colptr`**, plus millions of calls to `bcp_collen`. ODBC API calls have significant overhead (handle validation, state transitions, crossing the driver boundary).
- **Impact**: Core bottleneck. Making tens of millions of ODBC calls dominates the CPU time.
- **Remediation**:
  - **Bind to a fixed buffer:** Instead of updating the pointer (`bcp_colptr`) for every row, allocate a fixed-size buffer per column (e.g., `std::vector<uint8_t> buffer(stride)`). Bind this buffer *once* using `bcp_bind`. In the `row_loop`, simply `memcpy` the Arrow data into this buffer. This completely eliminates `bcp_colptr` calls for fixed-width columns.
  - **Stateful Length Updates:** Track the `current_len` for each column. Only call `bcp_collen` when the length or nullability *changes* (e.g., transitioning from a null value to a non-null value). For fixed-width columns, this reduces `bcp_collen` calls from $O(N)$ to $O(\text{null transitions})$.

### 2. String Allocation Overhead in `bind_columns` - **HIGH**
- **Evidence**: The `bind_columns` phase takes **3.1%** (0.138s). While not the primary bottleneck, it performs heavy memory allocation.
- **Root Cause**: For `arrow::Type::STRING` and `arrow::Type::LARGE_STRING`, the code iterates over the entire array and calls `typed->GetString(i)`, which allocates a new `std::string` for every single value, storing them in `utf8_cache`. For 1,000,000 rows, this is 1,000,000 heap allocations per string column.
- **Impact**: Causes memory fragmentation, unnecessary GC pressure, and amplifies cell processing overhead.
- **Remediation**:
  - Use `std::string_view` instead of `std::string`. Arrow's `StringArray` provides a `GetView(i)` method that returns a `std::string_view` pointing directly to the memory inside the Arrow buffer. Change `utf8_cache` to store `std::string_view` (or simply read the view directly inside the `row_loop` without caching it at all).

### 3. Eager Temporal Conversion in `bind_columns` - **MODERATE**
- **Evidence**: Date and Timestamp columns are eagerly converted into `SQL_DATE_STRUCT` and `SQL_TIMESTAMP_STRUCT` arrays during `bind_columns`.
- **Root Cause**: Arrow stores temporal data as integers (e.g., days since epoch, microseconds since epoch), but SQL Server BCP requires specific struct formats. The current code allocates a `std::vector` of these structs and populates it upfront.
- **Impact**: High memory usage and an extra iteration pass over the data.
- **Remediation**:
  - If the "fixed buffer" approach (from point 1) is adopted, these conversions can be done lazily inside the `row_loop`. Instead of allocating a massive `std::vector` upfront, convert the Arrow integer directly into the bound `SQL_DATE_STRUCT` buffer for the current row.

### 4. Batch Flush (`batch_flush`) - **EXTERNAL/TUNING**
- **Evidence**: `batch_flush` takes **41.9%** (1.860s) of the time.
- **Root Cause**: This is the time spent waiting for the SQL Server ODBC driver to transmit the data over the network and for SQL Server to process the inserts.
- **Impact**: Combines with row loop for 96.3% of write time.
- **Remediation**:
  - **Batch Size:** Finding the optimal batch size (often between 10k and 100k) is key.
  - **Table Hints:** Using the `TABLOCK` hint (e.g., `WITH (TABLOCK)`) can significantly speed up bulk inserts on the SQL Server side by reducing row-level locking overhead.

## Prioritized Remediation Plan

### P0 (Immediate)
- Raise default `batch_size` for Arrow BCP (e.g., 50k-100k).
- Add `TABLOCK` hint support to reduce server-side locking overhead during `batch_flush`.

### P1 (High Impact)
- **Reduce ODBC calls**: Implement the fixed-buffer binding strategy to eliminate `bcp_colptr` calls in the inner loop.
- **Optimize allocations**: Switch to zero-copy `std::string_view` for string columns and lazy conversion for temporal types.

### P2 (Structural)
- Add comprehensive benchmarks (rows, batch_size, modes) to track improvements against the 63.63 MB/s historical target.

## Acceptance Gates
- Reproduce runs at 200k/1M rows, batch_size 10k/50k/100k.
- Target: Approach 63.63 MB/s historical throughput.
- Track: rows/s, MB/s, prep/write phases.

### 5. Parallelization Potential (Data Conversion vs. Flushing)
- **Observation**: The current implementation is strictly single-threaded. It sequentially binds columns, loops over rows to send data (`bcp_sendrow`), and periodically flushes batches (`bcp_batch`).
- **Analysis**:
  - `bcp_sendrow` and `bcp_batch` are stateful operations on a single ODBC connection handle (`m_dbc`). ODBC connection handles are generally not thread-safe for concurrent operations on the same statement/connection.
  - However, the *preparation* of data (e.g., converting Arrow temporal integers to `SQL_DATE_STRUCT`, or extracting `std::string_view` from Arrow arrays) is entirely CPU-bound and independent of the ODBC handle.
  - The `batch_flush` phase (41.9% of time) is largely I/O bound (waiting for the network and SQL Server).
- **Feasibility**:
  - **Pipeline Architecture**: We could implement a producer-consumer pipeline.
    - **Producer Thread(s)**: Read from the Arrow RecordBatch, perform any necessary data conversions (e.g., temporal to struct, string extraction), and populate intermediate fixed-size buffers (e.g., one buffer per column per batch).
    - **Consumer Thread**: Takes the fully prepared buffers, binds them (`bcp_bind`), loops through the rows calling `bcp_sendrow` (which would now just be a fast pointer increment or `memcpy` if using the fixed-buffer approach), and calls `bcp_batch`.
  - **Challenges**:
    - **Memory Overhead**: We would need to allocate memory for at least two full batches of intermediate buffers (one being filled by the producer, one being sent by the consumer) to achieve true overlap.
    - **Complexity**: Managing thread synchronization, buffer handoffs, and error propagation (e.g., if the consumer fails to send, the producer must be stopped) adds significant complexity to the C++ extension.
    - **ODBC Limitations**: The actual `bcp_sendrow` and `bcp_batch` calls must still happen sequentially on a single thread per connection. If `bcp_sendrow` itself (even with optimized binding) remains the bottleneck, parallelizing the prep phase won't yield massive gains.
- **Recommendation**:
  - **Defer Parallelization**: Before introducing multi-threading complexity, implement the P1 optimizations (fixed-buffer binding to eliminate `bcp_colptr` calls, and zero-copy strings).
  - If the CPU-bound prep work (which is currently only ~3.1% of the time in `bind_columns`, but might increase slightly if we move temporal conversions to the `row_loop`) becomes the new bottleneck *after* fixing the ODBC call overhead, then a producer-consumer pipeline would be the next logical step.
