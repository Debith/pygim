import tabulate
from pygim.performance.dispatch import dispatch


@dispatch
def diff(__left, __right):
    """
    Compare two objects together visualizing differences.
    """
    raise NotImplementedError(f"No generic support yet!")


@diff.register(dict, dict)
def _diff_dict(_left, _right):
    """
    Compare two dictionaries visualizing differences.

    Parameters
    ----------
    _left : dict
        A dictionary to be compared with `_right`.
    _right : dict
        A dictionary to be compared with `_left`.

    Returns
    -------
    str
        A table of differences between `_left` and `_right`, visualizing the keys and values that are missing or mismatched.

    Notes
    -----
    This function returns a string representation of a table that shows the differences between two dictionaries.
    It compares the keys of the dictionaries, and for each key present in either dictionary, it displays the
    corresponding values side by side, highlighting any differences. If a key is present in one dictionary but not
    the other, the corresponding value is replaced with "<<MISSING>>" in the table.

    """
    lines = []
    for key in sorted(set(_right) | set(_left)):
        try:
            left = _right[key]
        except KeyError:
            left = "<<MISSING>>"

        try:
            right = _left[key]
        except KeyError:
            right = "<<MISSING>>"

        matching = "!=" if left != right else ""

        lines.append((key, left, matching, right))
    return tabulate.tabulate(lines)
