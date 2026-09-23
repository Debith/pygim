# -*- coding: utf-8 -*-
"""A memory as an agent needs it: a card that is always at hand, and a body read on demand.

ENACT's record format, chosen on 2026-09-23 from what is known about how models use what they are
given (global memory "Write a memory as a card and a body"):

- Text that is always loaded is paid for by every session, and a model follows fewer instructions
  the more it holds — favouring the early ones and failing mostly by omission (IFScale, 2025). So
  what is always delivered is small: the card.
- Detail must not be thrown away to get there. Summaries that compress lose the insight that made
  the knowledge worth keeping (ACE, 2025). So the body keeps it, one `show` away.
- A description that says what and *when* is what lets a model pick the right entry among many
  (Anthropic, Agent Skills), and a stated reason lets it apply a rule to cases the rule did not name
  (Anthropic, prompting guide). So a card carries `when`, `not` and `why`.
- A procedure is a goal and ordered steps, each with a check before the next (Agent Workflow Memory,
  2024; Agent Skills' checklists and feedback loops).

The fields are labelled lines at the head of the text — `When:`, `Not:`, `Do:`, `Why:`, `Asked:`,
then `Steps:` — so the text a person reads and the card a model receives are one record, parsed
each time, never two copies that can drift apart. The title is the rule itself, in the imperative.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any, Dict, List, Mapping, Sequence

# The card's fields, in the order a model needs them: does it apply, where does it stop, what to do,
# and why it matters. `asked` is for a procedure: the words a request uses when its task is asked.
FIELDS = ("when", "not", "do", "why", "asked")
LABELS = {"when": "When", "not": "Not", "do": "Do", "why": "Why", "asked": "Asked"}
REQUIRED = ("when", "why")

# What each field is for, in the words a refusal uses — a refusal names facts to act on.
MEANS = {
    "when": "the situations it applies to, in the words a request, a file or a task would use",
    "why": "one sentence on what goes wrong without it — a reason lets a rule reach cases it did not name",
    "not": "where it stops applying, so a short rule is not stretched over everything",
    "do": "the concrete action, command or check that carries it out",
    "asked": "comma-separated words a request uses when this procedure's task is asked, e.g. review, analyse",
    "steps": "ordered steps, each an action and the check that must hold before the next",
}

_FIELD = re.compile(r"^(When|Not|Do|Why|Asked)\s*:\s*(.*)$", re.IGNORECASE)
_STEPS = re.compile(r"^Steps\s*:\s*$", re.IGNORECASE)
_STEP = re.compile(r"^\s*\d+[.)]\s+(.*)$")
_ONE_LINE = re.compile(r"(?:\*\*)?One line:(?:\*\*)?\s*(.+)")


@dataclass
class Card:
    """The labelled head of a memory's text, and the rest of it."""

    when: str = ""
    not_: str = ""
    do: str = ""
    why: str = ""
    asked: str = ""
    steps: List[str] = field(default_factory=list)
    body: str = ""

    def get(self, name: str) -> str:
        return getattr(self, "not_" if name == "not" else name)

    @property
    def legacy(self) -> bool:
        """Written before the format existed: no labelled head at all."""
        return not any(self.get(name) for name in FIELDS) and not self.steps

    def missing(self, kind: str = "") -> List[str]:
        """The fields this card must have and does not. A procedure needs its steps as well."""
        gaps = [name for name in REQUIRED if not self.get(name).strip()]
        if kind == "procedure" and not self.steps:
            gaps.append("steps")
        return gaps

    def words(self) -> List[str]:
        """The request words of `asked`, lower-cased, in the order written."""
        return [w.strip().strip('"').lower() for w in self.asked.split(",") if w.strip().strip('"')]


def parse(text: str) -> Card:
    """Read the labelled head of *text*; everything after it is the body.

    The head is the run of labelled lines at the top: `Label: value` lines, an indented line
    continuing the one above, and after `Steps:` a numbered list. It ends at the first blank line.
    Text whose first line is not a label has no head — a memory written before the format — and
    is all body."""
    card = Card()
    lines = (text or "").strip("\n").splitlines()
    i, last, in_steps = 0, None, False
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            break
        match = _FIELD.match(line)
        step = _STEP.match(line) if in_steps else None
        if match:
            name = match.group(1).lower()
            setattr(card, "not_" if name == "not" else name, match.group(2).strip())
            last, in_steps = name, False
        elif _STEPS.match(line):
            in_steps, last = True, "steps"
        elif step:
            card.steps.append(step.group(1).strip())
        elif last and line[:1].isspace():               # a continuation of the line above
            if last == "steps" and card.steps:
                card.steps[-1] += " " + line.strip()
            elif last != "steps":
                attr = "not_" if last == "not" else last
                setattr(card, attr, (getattr(card, attr) + " " + line.strip()).strip())
        else:
            break
        i += 1
    if i == 0 or (last is None and not card.steps):
        return Card(body=(text or "").strip())
    card.body = "\n".join(lines[i:]).strip()
    return card


def compose(fields: Mapping[str, Any], body: str = "") -> str:
    """The canonical text for *fields* and *body*: labelled lines, then steps, then the body."""
    head = [f"{LABELS[name]}: {' '.join(str(fields[name]).split())}"
            for name in FIELDS if str(fields.get(name) or "").strip()]
    steps = [str(s).strip() for s in fields.get("steps") or [] if str(s).strip()]
    if steps:
        head.append("Steps:")
        head.extend(f"{n}. {' '.join(s.split())}" for n, s in enumerate(steps, 1))
    return "\n".join(head) + ("\n\n" + body.strip() if body and body.strip() else "")


def from_call(arguments: Mapping[str, Any]) -> Card:
    """The card a `remember` or `merge` call describes: its named fields over its text.

    Named fields win. A call that names none may carry the labelled head inside `text` instead,
    which is how a shell or an older client writes — the same record either way."""
    named = {name: arguments.get(name) for name in FIELDS if str(arguments.get(name) or "").strip()}
    if named or arguments.get("steps"):
        card = parse(compose({**named, "steps": arguments.get("steps") or []}))
        card.body = str(arguments.get("text") or "").strip()
        return card
    return parse(str(arguments.get("text") or ""))


def refusal(gaps: Sequence[str]) -> Dict[str, Any]:
    """What `remember` says to a call missing part of the card, as facts to act on."""
    return {"ok": False, "refused": "template",
            "message": "A memory is a card and a body: the card is what reaches a session unasked, so "
                       f"it must say {' and '.join(gaps)}. Pass them as fields; `text` is the body — "
                       "the example, the evidence, the links.",
            "facts": [f"{name}: {MEANS[name]}" for name in gaps]}


def kind_of(tags: Sequence[str]) -> str:
    return next((t.split("=", 1)[1] for t in tags or [] if t.startswith("kind=")), "")


def summary(text: str, room: int = 180) -> str:
    """For a memory written before the format: its `One line:` if it has one, else its opening
    sentence — something to go on until it is rewritten, never the whole text."""
    found = _ONE_LINE.search(text or "")
    line = found.group(1) if found else re.split(r"(?<=[.!?])\s", " ".join((text or "").split()), maxsplit=1)[0]
    line = line.strip().strip("*").strip()
    return line if len(line) <= room else line[: room - 1].rstrip() + "…"


def render(memory: Mapping[str, Any], scope: str = "project") -> str:
    """One memory as a card: its number and rule, then a line of when · not · do · why.

    A procedure's card names its task words and step count instead of the steps; the steps arrive
    in full when a request asks for that task, or with `show`."""
    ref = f"{memory['memory']}{' (global)' if scope == 'global' else ''}"
    card = parse(str(memory.get("text") or ""))
    head = f"{ref} {memory['title']}"
    if card.legacy:
        return f"{head}\n    {summary(card.body)} — not yet a card; `show` for the rest"
    parts = [f"{name}: {card.get(name)}" for name in ("when", "not", "do", "why") if card.get(name)]
    if card.steps:
        parts.append(f"{len(card.steps)} steps" + (f", asked as: {card.asked}" if card.asked else ""))
    return head + "\n    " + " · ".join(parts)


def asked_for(card: Card, prompt: str) -> int:
    """How many of a procedure's request words *prompt* uses — as whole words, with the usual
    endings, so `review` finds reviews and reviewing but `fix` does not find fixture."""
    text = " ".join((prompt or "").lower().split())
    hits = 0
    for word in card.words():
        pattern = r"(?<![\w-])" + re.escape(" ".join(word.split())) + r"(?:s|es|ed|d|ing|e)?(?![\w-])"
        if re.search(pattern, text):
            hits += 1
    return hits


def process(memory: Mapping[str, Any], scope: str = "project") -> str:
    """A procedure in full, as it is delivered at the moment its task is asked for."""
    card = parse(str(memory.get("text") or ""))
    ref = f"{memory['memory']}{' (global)' if scope == 'global' else ''}"
    task = next((t.split("=", 1)[1] for t in memory.get("tags", []) if t.startswith("task=")), "")
    lines = [f'<enact-process memory="{ref}" task="{task}">', str(memory["title"])]
    lines.append(" · ".join(f"{name}: {card.get(name)}" for name in ("when", "not") if card.get(name)))
    lines.append("Copy these steps into your reply as a checklist and tick them off as you go — "
                 "it is how the owner sees the process was followed:")
    lines.extend(f"{n}. {step}" for n, step in enumerate(card.steps, 1))
    if card.why:
        lines.append(f"why: {card.why}")
    lines.append(f"`show {memory['memory']}`{' in global' if scope == 'global' else ''} for the example and the evidence.")
    lines.append("</enact-process>")
    return "\n".join(line for line in lines if line)
