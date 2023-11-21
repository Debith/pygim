import os
import pathlib
import urllib
from pdm.backend.editable import EditableBuilder
from pdm.backend.wheel import WheelBuilder
from loguru import logger
from pprint import pformat


ROOT = pathlib.Path(__file__).parents[1]


def post_initialize(context):
    md = dict(context.config.metadata)
    md['dependencies'] = [urllib.parse.unquote(dep) for dep in md['dependencies']]
    logger.debug(f"Metadata: {pformat(md)}")
    context.config.metadata = context.config.metadata.__class__(md)
    context.config.data['project']['dependencies'] = [
        urllib.parse.unquote(dep) for dep in context.config.data['project']['dependencies']]


class GimmicksWheelBuilder(WheelBuilder):
    def initialize(self, context):
        # HACK: This handles issue with writing incorrect METADATA
        #       in Windows for custom paths.
        super().initialize(context)
        post_initialize(context)


class GimmicksEditableBuilder(EditableBuilder):
    def initialize(self, context):
        # HACK: This handles issue with writing incorrect METADATA
        #       in Windows for custom paths.
        super().initialize(context)
        post_initialize(context)


wheel_builder = GimmicksWheelBuilder(str(ROOT))
editable_builder = GimmicksEditableBuilder(str(ROOT))

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
    if rel_path.parts[0].startswith("pygim-cli"):
        rel_path = rel_path.relative_to(rel_path.parts[0])
    return str(rel_path), full_path


def build_package(builder, metadata_directory):
    context = builder.build_context(pathlib.Path(metadata_directory))
    if (
        not builder.config_settings.get("no-clean-build")
        or os.getenv("PDM_BUILD_NO_CLEAN", "false").lower() == "false"
    ):
        builder.clean(context)
    builder.initialize(context)
    files = [_trim_record(*f) for f in sorted(builder.get_files(context))]

    artifact = builder.build_artifact(context, files)
    builder.finalize(context, artifact)

    return str(artifact)

def build_wheel(
        metadata_directory,
        config_settings,
        *args,
        ):
    return build_package(wheel_builder, metadata_directory)


def build_editable(
        metadata_directory,
        config_settings,
        *args,
    ):
    return build_package(editable_builder, metadata_directory)
