"""
Per-indicator failure isolation for the `calculate_all_*` aggregators.

The `ta` library builds fixed-size intermediate arrays sized off the input
length minus the indicator's warm-up window, and indexes into them positionally.
On a series shorter than that warm-up it does not return NaN — it raises. ADX
on a one-month daily series (23 bars, window 14) raises

    IndexError: index 14 is out of bounds for axis 0 with size 10

and because the aggregators assigned columns in one straight-line block, that
single failure aborted the whole stage and took all thirty trend columns with
it. The daemon caught it one level up and recorded a `_trend_error` column, so
the terminal silently showed a technicals panel with no trend indicators at all
— and rated the stock off whatever momentum readings survived.

`safe_assign` contains the failure at the indicator that caused it. Everything
else in the stage still computes, and the casualty is written as NaN, which the
scorer already treats as "not available" and drops from the rating rather than
scoring as zero.
"""

import math


def safe_assign(result_df, columns, compute):
    """Write `compute()`'s output into `result_df` under `columns`.

    Args:
        result_df: DataFrame to write into.
        columns: Column name, or list of names when `compute` returns a dict.
        compute: Zero-arg callable returning a Series (single column) or a
                 dict of name → Series (multiple columns).

    Returns:
        `result_df`, with every requested column present — real values on
        success, NaN on failure.
    """
    if isinstance(columns, str):
        columns = [columns]

    try:
        value = compute()
    except Exception as exc:
        for col in columns:
            result_df[col] = math.nan
        # Containing the failure must not also hide it. The daemon's own
        # `_<stage>_error` sentinel only fires when a whole stage aborts, which
        # is exactly what this function now prevents — so without this marker
        # the terminal would show a confident rating over silently missing
        # indicators, which is the case the warning exists for. The C++ side
        # scans for `_<name>_error` columns and surfaces them; it ignores
        # non-numeric columns everywhere else.
        result_df["_{}_error".format(columns[0])] = str(exc)[:200]
        return result_df

    if isinstance(value, dict):
        for col in columns:
            result_df[col] = value.get(col, math.nan)
    else:
        # A Series answers for the first column only; anything else the caller
        # asked for is still owed a value, and NaN is the documented one.
        result_df[columns[0]] = value
        for col in columns[1:]:
            result_df[col] = math.nan

    return result_df
