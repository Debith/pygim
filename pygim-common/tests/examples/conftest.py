import pytest

from pygim.kernel.entangled_class import EntangledClassMetaMeta
from pygim.utils import safedelattr


@pytest.fixture(autouse=True)
def run_around_tests():
    safedelattr(EntangledClassMetaMeta, "_EntangledClassMetaMeta__namespaces")
    yield
    safedelattr(EntangledClassMetaMeta, "_EntangledClassMetaMeta__namespaces")