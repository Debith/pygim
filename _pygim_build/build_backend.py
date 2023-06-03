import sys
import pathlib
from setuptools.build_meta import build_sdist as setuptools_build_sdist
from setuptools.build_meta import build_wheel as setuptools_build_wheel
import setuptools_pep660
import toml

"""
[build-system]
requires = ["setuptools ~= 58.0", "wheel", "setuptools-pep660", "toml"]
build-backend = "build_backend"
backend-path = ["_pygim_build"]

def xxbuild_wheel(wheel_directory, config_settings=None, metadata_directory=None):
    sys.stdout.write(f"\n->{config_settings}\n")
    return setuptools_build_wheel(
        wheel_directory,
        config_settings=config_settings,
        metadata_directory=metadata_directory,
    )


def xprepare_metadata_for_build_wheel(config_settings):
    assert False
    return setuptools_build_wheel.get_requires_for_build_wheel(config_settings)

def xbuild_sdist(*args):
    assert False

def xprepare_metadata_for_build_editable(sdist_directory, config_settings=None):
    config_settings = toml.load(pathlib.Path(__file__).parent.parent / "pyproject.toml")['project']
    sys.stdout.write(f' -> {config_settings}')
    sys.stdout.write(f' -> {str(list(pathlib.Path(sdist_directory).rglob("*.*")))}')
    return setuptools_pep660.prepare_metadata_for_build_wheel(sdist_directory, config_settings)

def xget_requires_for_build_editable(*args):
    return setuptools_pep660.get_requires_for_build_editable(*args)

def _get_requires_for_build_wheel(*args):
    assert False

"""
def prepare_metadata_for_build_wheel(*args):
    assert False

def build_wheel(*args):
    assert False

def get_requires_for_build_editable(*args):
    assert False

def prepare_metadata_for_build_editable(*args):
    assert False

def build_editable(*args):
    assert False

def get_requires_for_build_sdist(*args):
    assert False

def build_sdist(*args):
    assert False


def xbuild_editable(
        wheel_directory,
        config_settings=None,
        metadata_directory=None,
        ):
    assert False
    result = setuptools_pep660.build_editable(wheel_directory, config_settings, metadata_directory)
    sys.stdout.write(f"\n\n{wheel_directory}\n{config_settings}\n{metadata_directory}\n\n")
    sys.stdout.write(str(list(pathlib.Path(wheel_directory).rglob("*.*"))))
    sys.stdout.write(f"\n\n{result}\n\n")
    return result
