// memory/strategy/files/base_vocabulary.h — the packaged activity vocabulary and memory facets (02 §1.1).
//
// Copied into a repository when it is created, never read from the installed
// tool afterwards: a newer pygim changes nobody's vocabulary behind their back
// (02 §1.1). The five dimensions follow the facet categories that survive in
// every field of work (overview §4.8).
#pragma once

#include <string_view>

namespace pygim::enact::strategy::files {

inline constexpr std::string_view base_vocabulary = R"yaml(# taxonomy v1 — shared activities with F3-derived request guidance.
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
      full: 'The activity the knowledge helps with. Designing and judging need different knowledge even about the
        same thing.

        '
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
        request:
          words:
          - design
          - propose
          - plan out
          - sketch
          - approach
          - how should
          - new
          give_if: Does the request ask for something that does not exist yet to be created or decided?
          not_if: Is the thing already there, and only to be judged or improved? (critique, evaluate)
      critique:
        entry:
          brief: Judging a thing that exists.
          when: The knowledge helps say what is good or bad about an existing thing.
          when_not: Not measuring against a fixed standard — that is evaluate.
          example: Pointing out that a contract clause can be read two ways.
        request:
          words:
          - review
          - analyze
          - critique
          - assess
          - what do you think
          - check this
          give_if: Does the request ask what is good or bad, right or wrong, about something that already exists?
          not_if: Is it measured against a fixed standard or number? (evaluate)
      evaluate:
        entry:
          brief: Measuring against a standard.
          when: The knowledge compares a thing with a stated benchmark or rule.
          when_not: Not an opinion about quality — that is critique.
          example: Checking a shelf's load against the manufacturer's rating.
        request:
          words:
          - measure
          - benchmark
          - compare
          - how much
          - how fast
          - verify against
          give_if: Does the request ask to measure or compare something against a stated standard, benchmark or rule?
          not_if: Is it an opinion about quality? (critique)
      troubleshoot:
        entry:
          brief: Finding why a thing fails.
          when: The knowledge helps locate the cause of a failure.
          when_not: Not improving a thing that works — that is design or critique.
          example: Tracing why a build fails only on Windows.
        request:
          words:
          - fix
          - bug
          - broken
          - fails
          - error
          - why does
          - not working
          give_if: Is something broken, failing or behaving wrongly, and its cause to be found?
          not_if: Does it work, and only need improving? (design, critique)
      explain:
        entry:
          brief: Making a thing understood.
          when: The knowledge helps someone understand how or why a thing works.
          when_not: Not changing or judging the thing — only understanding it.
          example: Why bread dough rises in a warm room.
        request:
          words:
          - explain
          - how does
          - what is
          - why
          - show me
          - summarize
          give_if: Is the goal only that the owner understands something — a question, a summary, what or how or why
            — with nothing judged, changed or written to a file?
          not_if: Is the explaining only part of doing another task? (that task) Must it end in a file a person reads
            later? (document)
      implement:
        entry:
          brief: Carrying out a decided change so that it works.
          when: The knowledge helps carry out a change whose goal is already decided.
          when_not: Not deciding what should be made — design; not only diagnosing a failure — troubleshoot.
          example: Applying an agreed repair and checking that it works.
        request:
          words:
          - implement
          - add
          - write
          - build
          - change
          - wire up
          - refactor
          give_if: Does the request ask to carry out, change, test or complete something whose goal is already decided?
          not_if: Is what to build still to be decided? (design) Is it a document for people? (document) Is it knowledge
            for future sessions? (record)
      document:
        entry:
          brief: Writing a thing down for a reader to open later.
          when: The knowledge helps write or maintain a document for people.
          when_not: Not an answer in conversation — explain; not knowledge for future agents — record.
          example: Writing instructions for maintaining a machine.
        request:
          words:
          - document
          - write up
          - write it into
          - markdown
          - report
          - readme
          - docs
          give_if: Does the request ask for a document, report, write-up or section to be written or changed? Must
            the work end in a file a person reads later?
          not_if: Is the answer meant for the conversation only? (explain) Is it knowledge for agents' memory? (record)
      discover:
        entry:
          brief: Finding out what the current project already has.
          when: The knowledge helps locate or understand existing resources before acting.
          when_not: Not investigating work outside the project — research.
          example: Finding the current design and its existing tests.
        request:
          words:
          - discover
          - discovery
          - best way
          - what do we have
          - already have
          - what exists
          - what is next
          - go through
          give_if: Can the request only be answered or done well after finding out what this project already has —
            what exists, the best way here, what is next?
          not_if: Is it about what is known outside the project? (research) Is the existing thing to be judged or
            measured? (critique, evaluate)
      research:
        entry:
          brief: Finding out what is known outside the project.
          when: The knowledge helps investigate external sources or alternatives.
          when_not: Not finding the current project's own resources — discover.
          example: Comparing published methods before choosing one.
        request:
          words:
          - research
          - study
          - options
          - alternatives
          - prior art
          - how do others
          - look into
          - state of the art
          - paper
          give_if: Does the request ask for the options or what others do, or bring outside work — a paper, a report,
            a library — to be weighed?
          not_if: Is it only about what this project already has? (discover)
      operate:
        entry:
          brief: Changing the state of something that runs.
          when: The knowledge helps start, stop, deploy or configure an operating system or process.
          when_not: Not changing its implementation — implement; not finding why it fails — troubleshoot.
          example: Restarting a service and checking that it is available.
        request:
          words:
          - kill
          - stop
          - start
          - restart
          - reload
          - run
          - serve
          - deploy
          - shut down
          give_if: Does the request ask to start, stop, restart, reload, deploy or configure something that runs?
          not_if: Is its implementation to be changed? (implement) Is the cause of a misbehaviour to be found? (troubleshoot)
      record:
        entry:
          brief: Capturing knowledge for later sessions.
          when: The knowledge helps maintain an agent's memory of facts, decisions or lessons.
          when_not: Not a document for people — document; not just explaining now — explain.
          example: Saving a verified lesson with its evidence in memory.
        request:
          words:
          - remember
          - memorize
          - record
          - store
          - lesson
          - write down
          - procedure
          - preference
          give_if: Does the request ask to remember, write down or store a rule, preference, lesson, decision or procedure
            for later sessions?
          not_if: Is it a document for people to read? (document)
      plan:
        entry:
          brief: Ordering work whose goal is already decided.
          when: The knowledge helps choose the next steps and their order.
          when_not: Not deciding what to build — design.
          example: Ordering the remaining steps of an agreed migration.
        request:
          words:
          - next steps
          - order
          - outstanding
          - what now
          - roadmap
          - merge order
          - priority
          give_if: Does the request ask for next steps, an order of work, a merge order, or what is still outstanding?
          not_if: Is what to build still to be decided? (design)
    request: true

  kind:
    role: soft
    weight: 0.5
    entry:
      brief: What sort of knowledge this is.
      full: >
        The epistemic type of a memory: a fact, a rule of thumb, a way of doing
        something, an instance, a choice made, a choice still open, or a
        taste. Two memories about the same thing can differ only in kind.
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
      question:
        entry:
          brief: An open question.
          when: The text records something to decide, with what is known so far, and no decision yet.
          when_not: Not a choice already made — that is decision.
          example: Whether the shelf should be fixed to the wall or stand free.

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

}  // namespace pygim::enact::strategy::files
