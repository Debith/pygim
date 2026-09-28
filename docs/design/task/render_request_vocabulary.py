"""Render the request-vocabulary draft two ways, from its one source:

- the value tables of 04_request_vocabulary.md, between its `<!-- values -->` markers;
- request-vocabulary.first-round.md, the compact vocabulary a first tagging round is handed:
  the dimensions a request can decide, every value, each as its tag, brief and words.

Values that exist today take their entries from the live stores (`oo enact call vocabulary`), so the
draft never restates them.

    ~/miniconda3/envs/dnd/bin/python docs/design/task/render_request_vocabulary.py
"""
import json
import subprocess
import sys
from pathlib import Path

import yaml

HERE = Path(__file__).resolve().parent
OO = Path(sys.executable).parent / "oo"
DRAFT = yaml.safe_load((HERE / "request-vocabulary.yaml").read_text(encoding="utf-8"))
FACETS = {"field": "domain", "activity": "task", "object": "artifact", "subject": "subject", "quality": "concern"}


def live(scope: str) -> dict:
    out = subprocess.run([str(OO), "enact", "call", "vocabulary", "--json", json.dumps({"scope": scope, "full": True})],
                         capture_output=True, text=True, check=True).stdout
    return {v["tag"]: v.get("entry") or {} for dim in json.loads(out)["dimensions"] for v in dim["values"]}


ENTRIES = {**live("global"), **live("project")}


def entry(value: dict) -> dict:
    return value.get("entry") or ENTRIES.get(value["tag"], {})


def cell(text: str) -> str:
    return str(text or "").replace("|", "\\|").replace("\n", " ")


def first_round() -> str:
    lines = ["# The request vocabulary for pygim — first round (draft, 2026-09-27)", "",
             "Split a request with these dimensions only. Several values of one dimension may be given. "
             "Hard dimensions filter what is retrieved; soft ones only order it. Each value: its brief, then words a request "
             "uses for it.", ""]
    for name, dim in DRAFT["dimensions"].items():
        if dim["round"] != "first":
            continue
        brief = (dim.get("entry") or {}).get("brief", "")
        lines += ["", f"## {name} ({dim['role']}) — {dim['facet']}" + (f": {brief}" if brief else "")]
        for v in DRAFT["values"]:
            if v["tag"].split("=")[0] == name:
                lines.append(f"- `{v['tag']}` — {entry(v).get('brief', '')} Words: {', '.join(v['words'])}.")
    return "\n".join(lines) + "\n"


def tables() -> str:
    out = []
    for name, dim in DRAFT["dimensions"].items():
        if dim["round"] != "first":
            continue
        vals = [v for v in DRAFT["values"] if v["tag"].split("=")[0] == name]
        new = sum(1 for v in vals if v.get("status") == "new")
        out += [f"### {dim['facet'].capitalize()} — `{name}` ({dim['role']}{', new' if dim.get('status') == 'new' else ''}): "
                f"{len(vals)} values, {new} new", "",
                "| Value | Status | Brief | Words | Not | Evidence | Resolves to |", "|---|---|---|---|---|---|---|"]
        for v in vals:
            e = entry(v)
            out.append(f"| `{v['tag'].split('=')[1]}` | {v.get('status', 'exists')} | {cell(e.get('brief'))} | "
                       f"{cell(', '.join(v['words']))} | {cell(e.get('when_not'))} | {', '.join(v.get('evidence', [])) or '—'} | "
                       f"{', '.join('`' + r + '`' for r in v.get('resolves', [])) or '—'} |")
        out.append("")
    return "\n".join(out)


if __name__ == "__main__":
    compact = first_round()
    (HERE / "request-vocabulary.first-round.md").write_text(compact, encoding="utf-8")
    doc = HERE / "04_request_vocabulary.md"
    text = doc.read_text(encoding="utf-8")
    start, end = "<!-- values:start -->", "<!-- values:end -->"
    head, rest = text.split(start, 1)
    doc.write_text(head + start + "\n\n" + tables() + "\n" + end + rest.split(end, 1)[1], encoding="utf-8")
    print(f"first round: {len(compact):,} characters; tables written into {doc.name}")
