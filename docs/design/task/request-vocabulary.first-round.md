# The request vocabulary for pygim — first round (draft, 2026-09-27)

Split a request with these dimensions only. Several values of one dimension may be given. Hard dimensions filter what is retrieved; soft ones only order it. Each value: its brief, then words a request uses for it.


## domain (hard) — field
- `domain=software` — Building software, in any project. Words: code, program, library, repository, build, test, command.
- `domain=pygim` — pygim itself — a C++26 and pybind11 library with a Python face. Words: pygim, oo, enact, pathlike, pathset, extension.

## task (hard) — activity
- `task=design` — Making something new. Words: design, propose, plan out, sketch, approach, how should, new.
- `task=critique` — Judging a thing that exists. Words: review, analyze, critique, assess, what do you think, check this.
- `task=evaluate` — Measuring against a standard. Words: measure, benchmark, compare, how much, how fast, verify against.
- `task=troubleshoot` — Finding why a thing fails. Words: fix, bug, broken, fails, error, why does, not working.
- `task=explain` — Making a thing understood now, in the answer. Words: explain, how does, what is, why, show me, summarize.
- `task=implement` — Carrying out a decided change to code so that it works. Words: implement, add, write, build, change, wire up, refactor.
- `task=document` — Writing a thing down for a reader to open later. Words: document, write up, write it into, markdown, report, readme, docs.
- `task=discover` — Finding out what the project already has before acting. Words: discover, discovery, best way, what do we have, already have, what exists, what is next, go through.
- `task=research` — Finding out what is known outside the project before deciding. Words: research, study, options, alternatives, prior art, how do others, look into, state of the art, paper.
- `task=operate` — Running, stopping or changing the state of something that runs. Words: kill, stop, start, restart, reload, run, serve, deploy, shut down.
- `task=record` — Capturing knowledge so that it outlives the task. Words: remember, memorize, record, store, lesson, write down, procedure, preference.
- `task=plan` — Ordering work that is already decided — what next, in which order. Words: next steps, order, outstanding, what now, roadmap, merge order, priority.

## artifact (hard) — object
- `artifact=extension` — A compiled extension module. Words: extension, module, compiled, manifest, pybind.
- `artifact=api` — A public interface. Words: interface, API, method, function, signature, stub, public.
- `artifact=service` — A long-lived object that owns state. Words: service, server object, lifecycle, state, thread.
- `artifact=store` — An on-disk format or layout. Words: store, on-disk, layout, format on disk, files it writes.
- `artifact=protocol` — A wire protocol. Words: protocol, JSON-RPC, messages, wire.
- `artifact=design_doc` — A design document. Words: design, document, section, overview, spec, doc.
- `artifact=test` — A test or a proof. Words: test, fixture, suite, assertion, proof.
- `artifact=benchmark` — A benchmark. Words: benchmark, timing, measurement, profile.
- `artifact=cli_command` — A command a user runs. Words: command, oo, flag, option, output, banner.
- `artifact=vocabulary` — A taxonomy pack or its codebook. Words: vocabulary, tag, dimension, value, pack, taxonomy.
- `artifact=release` — A published version of the library. Words: release, version, wheel, publish, PyPI.
- `artifact=report` — A document that presents findings for someone to read and act on. Words: report, findings, study, analysis, summary.
- `artifact=source_module` — One source file or module, as code — not its public surface. Words: this file, module, source, .py, .h, .cpp, class, function body.
- `artifact=web_page` — A page a program serves to a browser, and what the reader does on it. Words: page, button, browser, highlight, colour, scroll, click, view, served.
- `artifact=change` — A recorded change to the code — a commit, a branch, a pull request. Words: commit, branch, pull request, PR, merge, rebase, squash, push, stash, history.
- `artifact=process` — A program as it runs — a server, a session, a background job. Words: server, process, running, port, pid, session, background, job.
- `artifact=agent_context` — What an agent is given without asking — session-start text, hooks, instructions, prompts. Words: session start, hook, prompt, instructions, context, delivered, standing, mailbox.

## subject (soft) — subject: What the thing acted on is about, in the field's own words.
- `subject=configuration` — Settings a program reads from outside itself. Words: configuration, settings, environment variables, config, defaults, home directory, working directory.
- `subject=dependency_injection` — How the parts of a program are built and handed to each other. Words: composition root, wiring, dependency injection, container, IoC, factory, hidden dependency.
- `subject=software_design` — The shape of code — objects, responsibilities, patterns. Words: design pattern, strategy, domain model, domain-driven design, layering, adapter, responsibility, template.
- `subject=file_system` — Paths, directories and files on disk. Words: path, directory, folder, file system, walk, glob, relative path, uri.
- `subject=file_format` — How data is written into a file and read back. Words: yaml, json, toml, html, format, file type, parse, serialise, reader, writer, engine.
- `subject=command_line` — How a program is used from a terminal. Words: command line, CLI, command, option, flag, help, output, banner, terminal.
- `subject=web_ui` — What a served page shows and how a reader works with it. Words: page, browser, button, tint, scroll, served, localhost, comment on page.
- `subject=documentation` — What documents say, and how they are written and kept. Words: document, docs, section, scenario, overview, code comments, writing, style.
- `subject=diagrams` — Drawing structure or sequence as a picture. Words: diagram, sequence diagram, class diagram, mermaid, flowchart, arrows.
- `subject=version_control` — Recording, sharing and combining changes to code. Words: git, commit, branch, merge, rebase, pull request, push, stash, squash, history.
- `subject=continuous_integration` — What runs automatically on every change, and publishing its result. Words: CI, workflow, GitHub Actions, pipeline, release, publish, PyPI, matrix.
- `subject=packaging` — Building and installing the program on a machine. Words: build, install, conda, pip, wheel, pyproject, environment, compiler, editable.
- `subject=testing` — Tests as a subject — how a thing is proven. Words: test, fixture, isolation, pytest, suite, end to end, regression.
- `subject=data_structures` — How data is held in memory for fast use. Words: table, bit set, interning, index, id set, container, memory footprint.
- `subject=persistence` — Storing data in a database and reading it back. Words: database, SQL, ODBC, Arrow, bulk load, table, persistence.
- `subject=knowledge_retrieval` — Filing knowledge by tags and finding it again. Words: tags, vocabulary, taxonomy, classification, retrieval, citation, index, memory.
- `subject=agent_tooling` — How an AI agent is set up, fed and coordinated. Words: hook, session start, agent, subagent, MCP, prompt, instructions, mailbox.
- `subject=running_services` — Programs that keep running — servers, daemons, sessions — and their state. Words: server, process, restart, reload, port, running, background.

## concern (soft) — quality
- `concern=correctness` — Getting the right answer. Words: correct, wrong, right answer, bug.
- `concern=performance` — Time and space. Words: fast, slow, memory, speed, cost in time.
- `concern=portability` — Working the same everywhere. Words: Windows, macOS, platform, Python version.
- `concern=testing` — How a thing is proven. Words: testable, provable, isolation.
- `concern=documentation` — How a thing is explained. Words: documented, explained, readable docs.
- `concern=toolchain` — Compiling and linking. Words: compiler, linker, flags, toolchain.
- `concern=lifetime` — Who owns a thing, and for how long. Words: ownership, lifetime, dangling, reference.
- `concern=threading` — Threads, locks and the GIL. Words: thread, lock, GIL, concurrent.
- `concern=public_api` — The shape of what a caller sees. Words: public, caller sees, signature, breaking.
- `concern=usability` — How easily a person reads, finds or acts on something. Words: readable, easy, confusing, glance, find, review, understand.
- `concern=appearance` — How something looks. Words: look, cool, cooler, pretty, colour, style, visual.
- `concern=security` — Keeping data and access safe. Words: secure, encrypt, private, secret, leak, credentials.
- `concern=cost` — What a thing spends — tokens, money, a person's time. Words: tokens, cost, wasted, budget, expensive, money.
