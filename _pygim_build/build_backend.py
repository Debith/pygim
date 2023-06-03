import pathlib
from pdm.backend.editable import EditableBuilder

ROOT = pathlib.Path(__file__).parents[1]
builder = EditableBuilder(str(ROOT))

def build_editable(
        metadata_directory,
        config_settings,
        *args,
    ):
    return str(builder.build(metadata_directory))
