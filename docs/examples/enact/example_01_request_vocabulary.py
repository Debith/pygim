"""Use the packaged request guide and retrieve knowledge across activity tags.

The temporary store is the whole example's world. No user memory is read or changed.
"""
from pathlib import Path
from tempfile import TemporaryDirectory

from pygim.enact import Enact


with TemporaryDirectory() as directory:
    root = Path(directory) / "example-memory"
    Enact.init(str(root))
    memory = Enact(str(root))

    # request=True adds the first-round guide; memory codebook entries remain available.
    guide = memory.vocabulary(request=True)
    assert "task=implement" in guide["request"]
    assert "Give it if:" in guide["request"]
    assert "kind=principle" not in guide["request"]

    # A design constraint also helps an implementation request. Tag what the knowledge serves.
    saved = memory.remember(
        title="Keep callers on the public interface",
        text="An implementation must preserve the interface promised to its callers.",
        tags=["domain=any", "artifact=any", "task=design", "task=implement", "kind=principle"],
    )
    assert saved["ok"]

    # Scope with a known hard tag; an activity can rank knowledge without excluding other work.
    context = memory.read(hard=["domain=any"], soft=["task=implement"])
    assert context["ok"] and context["memories"][0]["memory"] == saved["memory"]

    steps = memory.remember(
        title="Apply a decided change",
        text="1. Apply the change.\n2. Verify the promised interface still holds.",
        tags=["domain=any", "artifact=any", "task=implement", "kind=procedure"],
        seen=[saved["memory"]],
    )
    assert steps["ok"]

    # The dedicated procedure slot needs one hard activity and one hard artifact.
    # This base-only store allows `any`: its domain and artifact have no concrete values yet.
    focused = memory.read(hard=["domain=any", "artifact=any", "task=implement"])
    assert focused["procedure"]["memory"] == steps["memory"]

print("ENACT request vocabulary OK")
