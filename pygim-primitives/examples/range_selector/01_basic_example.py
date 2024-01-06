from pygim.primitives import RangeSelector

# Range selector can be used to select a value from a range. In this example,
# we use it to select a grade based on a score. We define the ranges and the
# corresponding grades as a dictionary.
grade_ranges = {
    (0, 60): 'F',
    (60, 70): 'D',
    (70, 80): 'C',
    (80, 90): 'B',
    (90, 100): 'A',
}

# We create a range selector with the grade ranges.
grade_selector = RangeSelector(grade_ranges)

# We can now select a grade based on a score or range (as slice)
assert grade_selector[75] == 'C'
assert grade_selector[92] == 'A'
assert grade_selector[75:85] == 'D'

# Interface of the RangeSelector is close to builtins' interfaces such as int and range.
assert grade_selector.find('A') == (90, 100)
assert grade_selector.index('A') == (90, 100)

# Error messages are meaningful:
try:
    grade_selector.range_of('X')
except Exception as e:
    assert str(e) == 'Given input ``A`` not among: F, D, C, B, A'