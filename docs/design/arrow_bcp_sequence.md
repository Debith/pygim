@startuml ArrowPersistPath_try_c_stream
title ArrowPersistPath::try_c_stream – Full BCP Sequence

skinparam sequenceMessageAlign center
skinparam noteFontSize 11
skinparam sequenceParticipantPadding 10

participant Caller
participant ArrowPersistPath
participant "try_arrow_c_stream_bcp" as try_fn
participant "Polars / DataFrame\n(Python)" as Polars
participant import_arrow_reader
participant import_c_stream
participant "arrow::Import\nRecordBatchReader" as ArrowBridge
participant MssqlStrategyNative as MssqlStrategy
participant ensure_bcp_api
participant "ODBC Driver\n(libmsodbcsql)" as ODBCDriver
participant init_session
participant "arrow::RecordBatch\nReader" as RecordBatchReader
participant process_batch
participant bind_columns
participant classify_columns
participant setup_staging
participant run_row_loop
participant "BcpApi\n(bcp_*)" as BcpApi
participant finalize_bcp

== Phase 1: Convert Python DataFrame → C++ Arrow Reader ==

Caller -> ArrowPersistPath : try_c_stream(view)
note right
  Delegates to static helper,
  forwarding table, data_frame,
  batch_size, table_hint
end note

ArrowPersistPath -> try_fn : (strategy, table, data_frame,\nbatch_size, table_hint)

try_fn -> Polars : try_polars_compat_oldest()
note right
  Needed because Polars may encode
  strings as StringView (unsupported
  by some Arrow consumers).
  compat_level=oldest forces
  classic String/LargeString types.
end note
Polars --> try_fn : compat_level (or None)

alt Polars DataFrame (compat_level not None && has to_arrow)
    try_fn -> Polars : data_frame.to_arrow(compat_level=oldest)
    note right
      Converts Polars DataFrame
      to a PyArrow Table using
      the oldest wire format
      for maximum compatibility.
    end note
    Polars --> try_fn : arrow_table

    alt arrow_table has __arrow_c_stream__
        try_fn -> Polars : arrow_table.__arrow_c_stream__()
        note right
          Exports the table as a
          PyCapsule wrapping an
          ArrowArrayStream — zero-copy
          handoff to C++ without IPC.
        end note
    else arrow_table has to_reader
        try_fn -> Polars : arrow_table.to_reader()
        note right
          Fallback: creates a
          RecordBatchReader that can
          be exported to C stream.
        end note
    else fallback
        try_fn -> Polars : data_frame.__arrow_c_stream__()
    end

else Generic Arrow-compatible DataFrame
    try_fn -> Polars : data_frame.__arrow_c_stream__()
    note right
      Standard Arrow C Data
      Interface protocol —
      any compliant library
      (Pandas, PyArrow) supports this.
    end note
end

Polars --> try_fn : c_stream_capsule

try_fn -> import_arrow_reader : (c_stream_capsule)
note right
  Detects input type:
  1. PyCapsule (arrow_array_stream)
  2. _export_to_c exporter
  3. IPC bytes fallback
end note

import_arrow_reader -> import_arrow_reader : PyCapsule_CheckExact → true
note right
  Capsule confirmed — extract
  the raw ArrowArrayStream*
  pointer from the capsule.
end note

import_arrow_reader -> import_c_stream : (capsule, is_capsule=true)
import_c_stream -> import_c_stream : PyCapsule_GetPointer\n("arrow_array_stream")
note right
  Retrieves the native C
  ArrowArrayStream* stored
  inside the PyCapsule.
end note

import_c_stream -> ArrowBridge : ImportRecordBatchReader(stream)
note right
  Arrow C++ bridge takes ownership
  of the C stream and wraps it
  into a type-safe C++
  RecordBatchReader. This is the
  Python→C++ boundary crossing.
end note
ArrowBridge --> import_c_stream : shared_ptr<RecordBatchReader>
import_c_stream --> import_arrow_reader : reader
import_arrow_reader --> try_fn : {reader, "arrow_c_stream_capsule"}

== Phase 2: BCP Session Setup (pure C++, no Python refs) ==

try_fn -> MssqlStrategy : bulk_insert_arrow_bcp(table, reader,\nmode, batch_size, hint)

MssqlStrategy -> ensure_bcp_api : ensure_bcp_api()
note right
  Lazily dlopen() the ODBC driver
  SO and resolves bcp_* function
  pointers (init, batch, sendrow,
  colptr, collen, done, control).
  Cached as a static singleton.
end note
ensure_bcp_api --> MssqlStrategy : const BcpApi&

MssqlStrategy -> MssqlStrategy : ensure_connected()
note right
  Verifies ODBC connection
  (SQLHDBC) is alive. Reconnects
  if needed. BCP requires an
  active connection handle.
end note

MssqlStrategy -> ODBCDriver : enable_bcp_attr(dbc)
note right
  Sets SQL_COPT_SS_BCP = ON
  on the connection. Required
  before any bcp_* call —
  ODBC driver rejects BCP
  operations otherwise.
end note

MssqlStrategy -> MssqlStrategy : qualify_table(table)
note right
  Validates table name is
  non-empty. Returns as-is.
  Prevents passing garbage
  to bcp_init.
end note

MssqlStrategy -> init_session : (api, dbc, qualified, hint, batch_size)

init_session -> BcpApi : bcp_init(dbc, table, DB_IN)
note right
  Opens a BCP session targeting
  the table for bulk-in. The
  driver prepares server-side
  bulk insert infrastructure.
end note

opt table_hint is not empty (e.g. "TABLOCK")
    init_session -> BcpApi : bcp_control(BCPHINTS, hint)
    note right
      Passes hint like TABLOCK to
      the server. TABLOCK acquires
      a bulk-update lock, enabling
      minimal logging & parallelism.
    end note
end

opt batch_size > 0
    init_session -> BcpApi : bcp_control(BCPBATCH, batch_size)
    note right
      Tells the driver the intended
      batch size so it can optimize
      internal buffering and commit
      intervals server-side.
    end note
end

init_session --> MssqlStrategy : (session ready)

== Phase 3: Iterate Arrow Batches ==

loop reader->ReadNext(&batch) until nullptr
    MssqlStrategy -> RecordBatchReader : ReadNext(&batch)
    note right
      Reads the next columnar
      RecordBatch from the imported
      stream. Returns nullptr
      when exhausted.
    end note
    RecordBatchReader --> MssqlStrategy : batch (or nullptr → break)

    MssqlStrategy -> process_batch : process_batch(ctx, batch)

    process_batch -> bind_columns : bind_columns(bcp, dbc, batch)
    note right
      For each column, inspects Arrow
      type (INT64, STRING, DATE32…)
      and creates a ColumnBinding with:
      • data pointer into Arrow buffer
      • value stride (byte width)
      • null bitmap reference
      • string offset arrays
      Then calls bcp_bind to register
      each column with the BCP session.
    end note
    bind_columns --> process_batch : vector<ColumnBinding>

    process_batch -> classify_columns : classify_columns(bindings)
    note right
      Separates columns into fixed-width
      (int, double, date…) vs string.
      Also detects if ANY column has nulls.
      This drives the fast vs general
      row loop dispatch — avoiding null
      checks when none are needed.
    end note
    classify_columns --> process_batch : {fixed[], string[], any_has_nulls}

    process_batch -> setup_staging : setup_staging(bcp, dbc, fixed_cols)
    note right
      Allocates a single contiguous
      buffer for all fixed-width columns.
      Each column gets an offset into it.
      Calls bcp_colptr to point each
      column's BCP binding at its slot,
      and bcp_collen to set byte lengths.
      This avoids per-row colptr calls
      for fixed columns — pointer stays
      stable, only the data changes.
    end note
    setup_staging --> process_batch : staging buffer (vector<uint8_t>)

    process_batch -> run_row_loop : run_row_loop(ctx, fixed, string,\nstaging, num_rows, any_nulls)

    alt any_has_nulls == false (fast path)
        note over run_row_loop
          No null checks needed —
          straight memcpy + sendrow.
        end note
        loop for each row
            loop for each fixed column
                run_row_loop -> run_row_loop : memcpy(staging+offset,\nsrc+row*stride, stride)
                note right
                  Copies this row's fixed value
                  into the staging buffer. BCP
                  already points at that slot,
                  so no bcp_colptr call needed.
                end note
            end
            loop for each string column
                run_row_loop -> run_row_loop : handle_string_column(\nbcp, dbc, binding, row)
                note right
                  Reads string from Arrow offset
                  array, copies into reusable
                  str_buf (resizes if needed),
                  null-terminates, sets bcp_collen
                  to actual length. Only calls
                  bcp_colptr if buffer reallocated
                  (pointer changed).
                end note
            end
            run_row_loop -> BcpApi : bcp_sendrow(dbc)
            note right
              Submits the current row to the
              driver's internal buffer. The
              driver reads from the bound
              pointers (staging + string bufs).
            end note
            opt sent_rows % batch_size == 0
                run_row_loop -> BcpApi : bcp_batch(dbc)
                note right
                  Flushes accumulated rows to
                  the server. Commits the batch
                  so memory doesn't grow unbounded
                  and enables checkpoint/recovery.
                end note
            end
        end

    else any_has_nulls == true (general path)
        note over run_row_loop
          Must check null bitmap
          per column per row.
        end note
        loop for each row
            loop for each fixed column
                alt column has null at this row
                    run_row_loop -> BcpApi : bcp_collen(dbc,\nSQL_NULL_DATA, ordinal)
                    note right
                      Tells BCP this column is NULL
                      for the current row. Server
                      will store NULL instead of
                      reading from the data pointer.
                    end note
                else not null
                    run_row_loop -> run_row_loop : memcpy(staging+offset, src, stride)
                    opt column has_nulls flag set
                        run_row_loop -> BcpApi : bcp_collen(dbc, stride, ordinal)
                        note right
                          Restores collen after a prior
                          NULL — without this the driver
                          would still treat the column
                          as NULL from the last row.
                        end note
                    end
                end
            end
            loop for each string column
                alt column has null at this row
                    run_row_loop -> BcpApi : bcp_collen(dbc,\nSQL_NULL_DATA, ordinal)
                else not null
                    run_row_loop -> run_row_loop : handle_string_column(\nbcp, dbc, binding, row)
                end
            end
            run_row_loop -> BcpApi : bcp_sendrow(dbc)
            opt sent_rows % batch_size == 0
                run_row_loop -> BcpApi : bcp_batch(dbc)
            end
        end
    end

    run_row_loop --> process_batch : (done)
    process_batch --> MssqlStrategy : (batch processed)
end

== Phase 4: Finalize ==

MssqlStrategy -> MssqlStrategy : assert processed_rows > 0
note right
  Guard: if the Arrow reader
  yielded zero total rows, throw
  rather than silently committing
  an empty bulk insert.
end note

MssqlStrategy -> finalize_bcp : finalize_bcp(ctx)

opt sent_rows % batch_size != 0 (unflushed remainder)
    finalize_bcp -> BcpApi : bcp_batch(dbc)
    note right
      Flushes any remaining rows
      that didn't hit the batch_size
      boundary during the row loop.
    end note
end

finalize_bcp -> BcpApi : bcp_done(dbc)
note right
  Signals end of BCP session.
  Server commits all batches,
  releases bulk-insert locks,
  and updates statistics.
  Returns -1 on failure.
end note
BcpApi --> finalize_bcp : (ok)
finalize_bcp --> MssqlStrategy : (finalized)

MssqlStrategy --> try_fn : (returns)
try_fn --> ArrowPersistPath : PersistAttempt{success=true}
ArrowPersistPath --> Caller : PersistAttempt

@enduml