"""The inventory — ``oo inventory`` and the project map every session start carries — against the
ways it read before, each pair in one process (docs/definition_of_done.md: an A/B, never two runs).

Sections:

1. **imports** — every import of the project's own Python files: ``ast.parse`` and ``ast.walk`` (as
   ``imports_in`` read them until 2026-10-30) against pygim.code's parser (``imports_in`` now).
2. **names** — every installed distribution's name: importlib's ``.metadata["Name"]``, which parses
   the whole METADATA, against ``name_of``, which reads the Name header.
3. **environment** — what is installed, by module, by name and as a command (``installed``,
   ``installed_names``, ``commands_of``): as they asked until 2026-10-29 (``dist.files``, which
   stats every recorded file, and ``.metadata`` parsed on every ask); as they ask now, each
   distribution as importlib gives it; and through ``_ReadOnce``, each of its files read once.
4. **map** — ``project_map``, end to end.

The project is --root DIR, or else this repository.

Run:  python benchmarks/inventory.py [--root DIR] [--no-save]
Each run appends its raw measurements + environment metadata to
``results/inventory.jsonl`` (see ``_results.py``).
"""

import ast
import sys
from importlib import metadata
from pathlib import Path

from tabulate import tabulate

from _pygim import _inventory
from pygim import path
from _bench import REPS, best_fresh
from _results import corpus as corpus_name, save, wants_save


def argument(name):
    """The value after `name` on the command line, or None."""
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv[:-1] else None


def imports_by_ast(files):
    """The imports of `files` as ``imports_in`` found them over ast, uncounted."""
    found = []
    for relative, text in files:
        try:
            tree = ast.parse(text, filename=relative)
        except SyntaxError:
            continue
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                found += [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom) and node.module and not node.level:
                found.append(node.module)
    return found


def environment_as_it_was(dists):
    """installed, installed_names and commands_of until 2026-10-29: over ``dist.files`` and
    ``dist.metadata``, both asked anew each time."""
    tops, names, commands = {}, set(), set()
    for dist in dists:
        name = dist.metadata["Name"] if dist.metadata else None
        if not name:
            continue
        listed = (dist.read_text("top_level.txt") or "").split() or sorted(
            {Path(str(f)).parts[0] for f in (dist.files or [])
             if not str(f).startswith(("..", "__pycache__")) and Path(str(f)).suffix in (".py", ".so", ".pyd")})
        for top in listed:
            tops.setdefault(top.removesuffix(".py"), name)
    for dist in dists:
        if dist.metadata and dist.metadata["Name"]:
            names.add(_inventory.canonical(dist.metadata["Name"]))
    for dist in dists:
        name = dist.metadata["Name"] if dist.metadata else None
        if not name:
            continue
        groups = {entry.group for entry in (dist.entry_points or [])}
        if any(Path(str(f)).parent.name in ("bin", "Scripts") for f in (dist.files or [])) or \
                groups & {"console_scripts", "gui_scripts", "pytest11"}:
            commands.add(_inventory.canonical(name))
    return tops, names, commands


def contenders(root):
    """Each section's contenders, the way it was first: section -> {label: call}."""
    files = _inventory.python_files(root)
    ships = _inventory.ships_in(root)
    dists = list(metadata.distributions())
    asked = lambda ds: (_inventory.installed(ds), _inventory.installed_names(ds), _inventory.commands_of(ds))
    return {
        "imports": {"ast.parse + ast.walk": lambda: imports_by_ast(files),
                    "pygim.code (imports_in)": lambda: _inventory.imports_in(files, ships)},
        "names": {"metadata['Name']": lambda: [d.metadata["Name"] for d in dists],
                  "name_of (the header)": lambda: [_inventory.name_of(d) for d in dists]},
        "environment": {"as it was (dist.files, .metadata)": lambda: environment_as_it_was(dists),
                        "each file read per question": lambda: asked(dists),
                        "_ReadOnce": lambda: asked([_inventory._ReadOnce(d) for d in dists])},
        "map": {"project_map": lambda: _inventory.project_map(root)},
    }, {"files": len(files), "bytes": sum(len(text) for _, text in files), "distributions": len(dists)}


def main():
    given = argument("--root")
    root = path(given) if given else path(__file__).resolve().parent.parent
    sections, corpus = contenders(root)
    print(f"{corpus['files']} Python files ({corpus['bytes'] / 2**20:.1f} MB) under {root}, "
          f"{corpus['distributions']} distributions installed; best of {REPS}, ms\n")
    raw = {"corpus": {"root": corpus_name(root), **corpus}}
    for section, calls in sections.items():
        rows, raw[section] = [], {}
        for label, call in calls.items():
            t, _ = best_fresh(call)
            raw[section][label] = {"seconds": t}
            rows.append([label, f"{t * 1e3:,.1f}"])
        print(section + "\n" + tabulate(rows, headers=["", "ms"], disable_numparse=True) + "\n")
    if wants_save():
        print(f"Run recorded -> {save('inventory', raw, reps=REPS)}")


if __name__ == "__main__":
    main()
