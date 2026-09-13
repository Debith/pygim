// memory/strategy/files/base_vocabulary.h — taxonomy v0, as shipped (02, appendix).
//
// Copied into a repository when it is created, never read from the installed
// tool afterwards: a newer pygim changes nobody's vocabulary behind their back
// (02 §1.1). The five dimensions follow the facet categories that survive in
// every field of work (overview §4.8).
#pragma once

#include <string_view>

namespace pygim::memory::strategy::files {

inline constexpr std::string_view base_vocabulary = R"yaml(# taxonomy v0 — the base every repository starts from.
# Packs add values to domain, artifact and tier, and dimensions of their own.
pack: base

dimensions:
  domain:
    role: hard
    weight: 1.0
    entry:
      brief: The field the knowledge belongs to.
      full: >
        Which body of work the knowledge is about. Each domain is prepared from
        its documents and adds a pack; the domain's name is the pack's name.
      when: Every memory answers it — knowledge always belongs to some field.
      when_not: Not to say what the knowledge is about within the field — that is artifact.
      example: A note on sourdough hydration is domain=baking; one on reading a lease is domain=tenancy.

  artifact:
    role: hard
    weight: 1.0
    entry:
      brief: What is being made or examined.
      full: >
        The kind of thing the knowledge concerns. The values are the domain's
        own: loaves and ovens in one, leases and clauses in another.
      when: The knowledge is about one kind of thing the domain produces or studies.
      when_not: Not the activity performed on the thing — that is task.
      example: A rule about shaping loaves is artifact=bread in a baking domain.

  task:
    role: hard
    weight: 1.0
    entry:
      brief: What the knowledge is for doing.
      full: >
        The activity the knowledge helps with. Designing and judging need
        different knowledge even about the same thing.
      when: The knowledge helps someone make, judge, measure, fix or explain something.
      when_not: Not the thing acted on — that is artifact.
      example: A checklist used while drafting a new lease clause is task=design.
    values:
      design:
        entry:
          brief: Making something new.
          when: The knowledge helps create a thing that does not yet exist.
          when_not: Not judging or improving a thing that exists — that is critique or evaluate.
          example: Choosing the dimensions of a bookshelf that does not exist yet.
      critique:
        entry:
          brief: Judging a thing that exists.
          when: The knowledge helps say what is good or bad about an existing thing.
          when_not: Not measuring against a fixed standard — that is evaluate.
          example: Pointing out that a contract clause can be read two ways.
      evaluate:
        entry:
          brief: Measuring against a standard.
          when: The knowledge compares a thing with a stated benchmark or rule.
          when_not: Not an opinion about quality — that is critique.
          example: Checking a shelf's load against the manufacturer's rating.
      troubleshoot:
        entry:
          brief: Finding why a thing fails.
          when: The knowledge helps locate the cause of a failure.
          when_not: Not improving a thing that works — that is design or critique.
          example: Tracing why a build fails only on Windows.
      explain:
        entry:
          brief: Making a thing understood.
          when: The knowledge helps someone understand how or why a thing works.
          when_not: Not changing or judging the thing — only understanding it.
          example: Why bread dough rises in a warm room.

  kind:
    role: soft
    weight: 0.5
    entry:
      brief: What sort of knowledge this is.
      full: >
        The epistemic type of a memory: a fact, a rule of thumb, a way of doing
        something, an instance, a choice made, or a taste. Two memories about
        the same thing can differ only in kind.
      when: Every memory answers it.
      when_not: Never to say how good the knowledge is — valence is not kind.
      example: '"Measure twice, cut once" is a principle; "the pine shelf that sagged" is an example.'
    values:
      reference:
        entry:
          brief: A fact that can be looked up.
          when: The text states something true that a source would confirm.
          when_not: Not a rule for judging — that is a principle.
          example: Water boils at 100 °C at sea level.
      principle:
        entry:
          brief: A rule of thumb.
          when: The text states a general rule that guides decisions.
          when_not: Not a sequence of steps — that is a procedure.
          example: A new tool earns its place only if it beats the one it replaces.
      procedure:
        entry:
          brief: Ordered steps by which something is achieved.
          full: >
            How a thing is done in this domain, step by step, for one artifact
            and one task. Placed first in every context for that pair.
          when: The text is a sequence the reader should follow in order.
          when_not: A single rule, however important — that is a principle.
          example: 'Replacing a fuse: switch off the mains, find the blown fuse, fit one of the same rating, switch on, test.'
      example:
        entry:
          brief: One instance.
          when: The text describes a particular case.
          when_not: Not a generalisation drawn from cases — that is a principle.
          example: The pine shelf sagged because its span was over 80 cm.
      decision:
        entry:
          brief: A choice made, and why.
          when: The text records what was chosen among alternatives and the reason.
          when_not: Not a rule for all cases — that is a principle.
          example: Oak was chosen over pine for the shelf because of the load it carries.
      preference:
        entry:
          brief: A taste.
          when: The text records how someone likes things done.
          when_not: Not a claim about correctness — that is a principle.
          example: Prefer a written agenda before any meeting.

  tier:
    role: soft
    weight: 1.0
    entry:
      brief: The level or maturity the knowledge applies to.
      full: >
        Where on the domain's scale of level, size or maturity the knowledge
        holds. Packs name the bands.
      when: The knowledge holds at some levels and not others.
      when_not: Not how important the knowledge is.
      example: A technique only for experienced bakers is tier=advanced in a baking domain.
)yaml";

}  // namespace pygim::memory::strategy::files
