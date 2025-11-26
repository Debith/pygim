from pygim.arrow_bridge import (
    iter_record_batches,
    read_ipc_file,
    to_arrow_table,
    to_ipc_bytes,
    write_ipc_file,
    from_ipc_bytes,
)

import pathlib

import pytest

arrow = pytest.importorskip("pyarrow")
polars = pytest.importorskip("polars")


def _make_df():
    return polars.DataFrame({
        "id": [1, 2, 3],
        "name": ["Ada", "Grace", "Linus"],
        "scores": [0.9, 0.8, 0.95],
    })


def test_to_arrow_table_roundtrip():
    df = _make_df()
    table = to_arrow_table(df)
    assert isinstance(table, arrow.Table)
    assert table.to_pydict() == df.to_dict(as_series=False)


def test_ipc_bytes_roundtrip(tmp_path: pathlib.Path):
    df = _make_df()
    ipc_bytes = to_ipc_bytes(df)
    table = from_ipc_bytes(ipc_bytes)
    assert table.to_pydict() == df.to_dict(as_series=False)

    out_path = write_ipc_file(df, tmp_path / "payload.arrow")
    assert out_path.exists()

    table_from_disk = read_ipc_file(out_path)
    assert table_from_disk.to_pydict() == df.to_dict(as_series=False)


def test_iter_record_batches_from_bytes():
    df = _make_df()
    ipc_bytes = to_ipc_bytes(df)
    batches = list(iter_record_batches(ipc_bytes))
    assert len(batches) == 1
    assert batches[0].column(0).to_pylist() == [1, 2, 3]


def test_iter_record_batches_from_table():
    df = _make_df()
    table = to_arrow_table(df)
    batches = list(iter_record_batches(table))
    assert len(batches) == 1
    assert batches[0].num_columns == 3
