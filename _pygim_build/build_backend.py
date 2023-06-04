import os
import sys
import pathlib
from pdm.backend.editable import EditableBuilder
from pdm.backend.wheel import WheelBuilder

ROOT = pathlib.Path(__file__).parents[1]

wheel_builder = WheelBuilder(str(ROOT))
editable_builder = EditableBuilder(str(ROOT))

# KNOWN HOOKS
# get_requires_for_build_wheel(config_settings)
# prepare_metadata_for_build_wheel(metadata_directory, config_settings)
# build_wheel(wheel_directory, config_settings, metadata_directory)
# get_requires_for_build_editable(config_settings)
# prepare_metadata_for_build_editable(metadata_directory, config_settings)
# build_editable(metadata_directory, config_settings)
# get_requires_for_build_sdist(config_settings)
# build_sdist(sdist_directory, config_settings)


def prepare_metadata_for_build_wheel(metadata_directory, config_settings):
    return str(wheel_builder.prepare_metadata(metadata_directory))

def _trim_record(rel_path, full_path):
    rel_path = pathlib.Path(rel_path)
    if rel_path.parts[0].startswith("pygim-"):
        rel_path = rel_path.relative_to(rel_path.parts[0])
    return str(rel_path), full_path


def build_wheel(
        metadata_directory,
        config_settings,
        *args,
        ):

    context = wheel_builder.build_context(pathlib.Path(metadata_directory))
    if (
        not wheel_builder.config_settings.get("no-clean-build")
        or os.getenv("PDM_BUILD_NO_CLEAN", "false").lower() == "false"
    ):
        wheel_builder.clean(context)
    wheel_builder.initialize(context)
    files = [_trim_record(*f) for f in sorted(wheel_builder.get_files(context))]

    artifact = wheel_builder.build_artifact(context, files)
    wheel_builder.finalize(context, artifact)

    return str(artifact)

def build_editable(
        metadata_directory,
        config_settings,
        *args,
    ):
    editable_builder.initialize()
    return str(editable_builder.build(metadata_directory))
