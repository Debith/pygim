import importlib
import pytest
from pygim.fileio import PathSet
from pathlib import Path

ROOT = Path(__file__).parents[3]

EXAMPLES = PathSet(ROOT / 'docs/examples').files(suffix=".py"   )
assert EXAMPLES
EXAMPLES = list(reversed(sorted(EXAMPLES)))

@pytest.mark.parametrize("module_path", EXAMPLES)
def test_examples(module_path):
    spec = importlib.util.spec_from_file_location(module_path.stem, module_path)
    example_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(example_module)


if __name__ == "__main__":
    from pygim.testing import run_tests
    run_tests(__file__, module_name="pygim", coverage=False)