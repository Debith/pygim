from pathlib import Path
import importlib
import pytest

ROOT = Path(__file__).parents[2]

EXAMPLES = list(ROOT.joinpath('docs/examples').rglob('*.py'))
assert EXAMPLES
EXAMPLES = list(reversed(sorted(EXAMPLES)))

@pytest.mark.parametrize("module_path", EXAMPLES)
def test_examples(module_path):
    spec = importlib.util.spec_from_file_location(module_path.stem, module_path)
    example_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(example_module)


if __name__ == "__main__":
    from pygim.core.testing import run_tests
    run_tests(__file__)