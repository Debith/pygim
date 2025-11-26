"""Deprecated save helpers.

This module previously exposed ``save_dataframe_with_strategy`` but the helper
has been removed in favour of calling the strategy methods directly.
"""

raise RuntimeError(
    "pygim.repo.strategy_ops has been removed; use the bulk_* methods on your "
    "strategy directly (bulk_insert, bulk_upsert, or bulk_insert_arrow_bcp)."
)
