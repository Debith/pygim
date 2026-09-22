import pytest
import tempfile
import pathlib

ROOT = pathlib.Path(__file__).parents[3]


@pytest.fixture
def temp_dir():
    tdir = tempfile.TemporaryDirectory()
    __tdir = pathlib.Path(tdir.name)

    yield __tdir

    assert __tdir.exists(), "DO NOT DELETE TEMP DIR!"

    tdir.cleanup()


@pytest.fixture(autouse=True)
def never_this_machine(tmp_path, monkeypatch):
    """No test reaches this machine's own stores, whether it remembered to arrange that or not.

    A test that writes into the developer's real store is worse than a failing test: it is a
    passing one that has changed their data. It happened on 2026-09-22 — a store called `proj`
    appeared under the real user data directory, from a suite run mid-refactor where the code had
    moved on and one fixture had not. Nothing about that run announced it.

    So the redirection is here, automatic, for every test in the suite. A test that wants a
    particular configuration still builds an `Environment` and passes it in; this is only the floor
    under the ones that forget, and under the subprocesses, whose composition root reads these for
    itself exactly as a person's shell would give them.
    """
    home = tmp_path / "home-of-the-test"
    home.mkdir(exist_ok=True)
    for name, value in {"HOME": str(home),
                        "USERPROFILE": str(home),          # Windows
                        "XDG_DATA_HOME": str(tmp_path / "data-of-the-test"),
                        "LOCALAPPDATA": str(tmp_path / "data-of-the-test"),
                        "PYGIM_ENACT_ROOT": "", "PYGIM_MEMORY_ROOT": "",
                        "PYGIM_ENACT_GLOBAL": str(tmp_path / "no-global-store"),
                        "PYGIM_MEMORY_GLOBAL": "",
                        "PYGIM_ENACT_SESSION": "", "PYGIM_MEMORY_SESSION": ""}.items():
        monkeypatch.setenv(name, value)
