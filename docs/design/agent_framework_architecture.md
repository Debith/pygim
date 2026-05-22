# Agent Runtime - Architecture

Current as of May 2026. Single source of truth for the future multi-agent runtime
design under `pygim.ai.runtime`. This document is design-only and does not imply
that implementation has started.

**Diagrams** (PlantUML, same directory):

- `agent_framework_layered_architecture.puml` - Layer architecture overview
- `agent_framework_class_diagram.puml` - Detailed class and component diagram
- `agent_framework_mediator_class_diagram.puml` - Focused mediator core class diagram
- `agent_framework_directed_sequence.puml` - Directed request and reply flow
- `agent_framework_broadcast_sequence.puml` - Broadcast fanout flow
- `agent_framework_tool_sequence.puml` - Tool invocation flow
- `agent_framework_variation_diagram.puml` - Strategy and policy variation surface
- `agent_framework_runtime_diagram.puml` - Runtime threads and queues overview

---

## 1. Directory Layout

```text
src/_pygim_fast/
|- agent_runtime/
|  |- core/
|  |  |- types.h
|  |  |- envelope.h
|  |  |- bounded_queue.h
|  |  |- agent_slot.h
|  |  |- reply_tracker.h
|  |  `- mediator.h
|  |- adapter/
|  |  |- python_codec.h
|  |  |- context.h
|  |  |- tool_bridge.h
|  |  |- tool_executor.h
|  |  |- diagnostics_dispatcher.h
|  |  |- agent_worker.h
|  |  |- agent_system.h
|  |  `- bindings.cpp
|  `- event_bus/
|     |- core/
|     |  |- transport_types.h
|     |  |- inbound_event_bus.h
|     |  `- outbound_event_bus.h
|     |- adapter/
|     |  |- inbound_event_bus_adapter.h
|     |  `- outbound_event_bus_adapter.h
|     `- strategy/
|        `- zeromq/
|           |- fetch_strategy.h
|           |- publish_strategy.h
|           |- envelope_deserializer.h
|           `- envelope_serializer.h
|- ext.agent_runtime.toml

possible future phase 2 optional module shape only if explicit build gating and
feature-loading are added later:
`- ext.agent_runtime_zmq.toml

src/_pygim/_core/agent_protocols.py
src/_pygim/_core/agent_protocols.pyi
src/pygim/ai/runtime.py
```

### Build Shape

| TOML file | Status | Purpose |
| --------- | ------ | ------- |
| `ext.agent_runtime.toml` | Planned v1 module shape | In-process mediator runtime |
| `ext.agent_runtime_zmq.toml` | Possible phase 2 optional module shape only | Optional ZeroMQ transport strategies for inbound and outbound event buses, only if explicit build gating and feature-loading are added later |

- `core/` is C++23, pybind-free, and owns routing semantics.
- `adapter/` is the only pybind11 boundary and the only layer allowed to hold
  Python callables or `py::object`.
- `event_bus/core/` is C++23, pybind-free, and owns `TransportMessage`,
  `PublishReceipt`, and the explicit inbound and outbound bus cores.
- `event_bus/adapter/` owns polling and publish loops plus Python hook bridging
  so transport waits never block `MediatorCore`.
- `event_bus/strategy/zeromq/` is phase 2 only and remains adapter-owned. It is
  not owned by `MediatorCore` and it is not part of the v1 runtime contract.
- The public Python surface stays under `pygim.ai.runtime` to avoid namespace
  confusion with repository automation directories.

---

## 2. Namespace Mapping

| Path / Directory | Namespace / Module | Responsibility |
| ---------------- | ------------------ | -------------- |
| `agent_runtime/core/` | `pygim::agent_runtime::core` | Message types, bounded queues, reply tracking, mediator routing |
| `agent_runtime/adapter/` | `pygim::agent_runtime::adapter` | pybind11 edge, Python codec, contexts, agent workers, diagnostics dispatch, tool bridge |
| `agent_runtime/event_bus/core/` | `pygim::agent_runtime::event_bus::core` | Transport-neutral bus types plus explicit inbound and outbound event bus cores |
| `agent_runtime/event_bus/adapter/` | `pygim::agent_runtime::event_bus::adapter` | Adapter-owned polling and publish loops, direct ingress submission, outbound buffering, and Python hook bridging |
| `agent_runtime/event_bus/strategy/zeromq/` | `pygim::agent_runtime::event_bus::strategy::zeromq` | Optional phase 2 transport strategies for fetch and publish |
| `src/_pygim/_core/agent_protocols.py` | `_pygim._core.agent_protocols` | Thin Python wrapper/control-plane protocols for inbound and outbound event buses around adapter-owned collaborators |
| `src/pygim/ai/runtime.py` | `pygim.ai.runtime` | Thin public API and re-exports |

The namespace split follows existing repo conventions: core stays reusable and
GIL-free, the adapter is thin, and the public Python layer remains ergonomic
glue instead of duplicating runtime logic. The Python bus protocols stay
deliberately narrower than the loop-owning C++ adapters: they describe only the
wrapper or control-plane surface exposed to Python.

---

## 3. Message Model and Result Types

| Type | Purpose | Notes |
| ---- | ------- | ----- |
| `TransportMessage` | Transport-neutral bus unit | Carries transport headers and opaque bytes only; fetched or published by transport strategies |
| `Envelope` | Single routed unit | Carries `MessageId`, sender, optional recipient, optional topic, `DeliveryKind`, `EncodedPayload`, optional correlation id, and optional `ToolDescriptor` |
| `EncodedPayload` | Adapter-safe payload container | Core sees bytes plus codec metadata only; payload bytes stay opaque below the adapter |
| `ToolDescriptor` | Immutable tool call metadata | Tool name, version or schema id, timeout hint, and execution flags used by the adapter and executor |
| `MessageId` | Opaque correlation key | Allocated in core and used by `ReplyTracker` for both request/reply and tool replies |
| `Duration` | Strong timeout or interval value | Common core duration type returned by custom C++ literals such as `15_ms` and `100_ns`; arithmetic stays in `Duration` |
| `Deadline` | Strong monotonic deadline value | Absolute runtime deadline value; serialized as normalized nanoseconds only at the transport edge |
| `DeliveryKind` | Routing intent enum | `Directed`, `Broadcast`, `Request`, `Reply`, `ToolRequest`, `ToolReply`, `Diagnostic` |
| `PublishReceipt` | Transport publish result | Small transport-level outcome; backend metadata stays outside mediator routing semantics |
| `BroadcastResult` | Per-publish delivery summary | Returned by `publish()` with `delivered_count` and `dropped_count` |
| `OverflowMode` | Future queue policy note | v1 fixes every queue to `RejectNew`; `DropOldest` remains out of scope until a later design proves it safe |

### TransportMessage

`TransportMessage` is the transport-neutral unit used by the event bus cores.
`InboundFetchStrategy` returns `TransportMessage` or `None`.
`OutboundSerializationStrategy` produces it from an `Envelope`. Kafka records,
ZeroMQ frames, or smoke signals are just strategy implementations behind this
type. It carries transport headers and opaque bytes only.

### Minimum Envelope <-> TransportMessage mapping

The event bus needs one transport-neutral metadata contract between `Envelope`
and `TransportMessage`. The normalized metadata or header keys below are
conceptual names, not transport-specific wire fields.

| Envelope field | TransportMessage mapping | Notes |
| -------------- | ------------------------ | ----- |
| `message_id` | Required normalized metadata `message_id` | Stable identifier across local and remote hops |
| `correlation_id` | Optional normalized metadata `correlation_id` | Required for reply-like traffic |
| `kind` | Required normalized metadata `kind` | Describes runtime delivery intent, not transport verbs |
| `sender` | Required normalized metadata `sender` | Logical runtime or agent identity |
| `recipient` or `topic` | Exactly one normalized route key: `recipient` for directed or request-reply-like traffic, `topic` for broadcast | First-cut remote egress never sets both |
| `deadline` | Optional normalized metadata `deadline_ns` | `Deadline` serialized as normalized nanoseconds when the envelope carries one |
| `tool` metadata | Optional normalized metadata `tool.name`, `tool.schema_id`, `tool.timeout_ns`, and `tool.flags` | Present only when tool routing metadata exists; `Duration` is normalized to nanoseconds at the transport edge |
| `payload.codec_name` | Required normalized metadata `codec_name` | Kept distinct from content type |
| `payload.content_type` | Optional normalized metadata `content_type` | MIME-like or application-specific description |
| `payload.bytes` | `TransportMessage.bytes` body | Opaque payload bytes; metadata never embeds payload content |

### Envelope

`Envelope` is the only unit the mediator routes. It must be fully self-describing
from core metadata:

- `message_id`
- `correlation_id` for replies
- `sender`
- `recipient` for directed and request traffic
- `topic` for broadcast traffic
- `delivery_kind`
- `payload`
- `tool` for tool requests
- optional `deadline`

No `py::object` crosses below the adapter. The mediator routes on ids, topics,
delivery kind, deadline metadata, and queue state only.

### Temporal types

Core time values must use strong value types rather than raw integers with unit
suffixes baked into field names.

- `Duration` is the common interval type for all core timeout arithmetic.
- Custom C++ literals `_ns`, `_us`, `_ms`, and `_s` all produce `Duration`, so
  `15_ms + 100_ns` stays strongly typed.
- `Deadline` is a distinct monotonic runtime deadline type, not a naked
  `int64_t`.
- Python-facing `timeout_ms` arguments remain adapter convenience only; the
  adapter converts them to `Duration` immediately on entry to the core.

Illustrative C++ shape:

```cpp
using namespace pygim::agent_runtime::core::literals;

Duration timeout = 15_ms + 100_ns;
```

### EncodedPayload

The default codec is `JsonOrBytesCodec`. It is sufficient for control messages,
small structured payloads, and raw bytes without forcing Arrow or Python object
graphs into the core. `ArrowIpcCodec` is a future option only for large tabular
payloads and should remain off the v1 critical path.

`CodecStrategy` is the Python payload seam: `py::object <-> EncodedPayload`.
Event bus deserialization and serialization are separate seams:
`TransportMessage <-> Envelope`. Those transport serializers and deserializers
must not touch `py::object`.

### ToolDescriptor

`ToolDescriptor` is metadata, not the tool implementation itself. The actual tool
callable remains in the adapter layer. Its timeout field should be a strong
`Duration`, not a raw millisecond count. This keeps core routing deterministic
and keeps Python execution at the edge where it belongs.

### PublishReceipt

`PublishReceipt` is the small transport-level result returned by an outbound
publish strategy. It may carry backend-specific metadata, but it does not alter
local routing, reply tracking, or mediator overload semantics.

### BroadcastResult

`BroadcastResult` reports the outcome of one `publish()` call:

- `delivered_count`
- `dropped_count`

It is intentionally small. Recipient-level diagnostics remain best-effort and do
not expand the blocking surface of the public API.

### OverflowMode

`OverflowMode` is kept only as a future policy note. v1 fixes every queue to
`RejectNew` and does not expose runtime selection. `DropOldest` remains out of
scope until a later design proves it safe across broadcast and reply tracking.

---

## 4. Key Design Decisions

### 4.1 In-process mediator first

The default transport choice is a custom in-process mediator with bounded queues.
That is the v1 product, not a fallback. The goal is to prove routing semantics,
tool isolation, reply tracking, and overload behavior inside one process before
adding any network surface area.

### 4.2 Fixed routing semantics, explicit inbound and outbound event buses

The core should not be designed around one generic transport seam. Routing
semantics stay custom and fixed. Transport integration, when present, stays
adapter-owned and is split into two explicit collaborators with separate
compile-time surfaces:

- `InboundEventBusCore<FetchStrategy, DeserializationStrategy, EnableFetchHooks, EnableDeserializationHooks>`
- `OutboundEventBusCore<SerializationStrategy, PublishStrategy, EnableSerializationHooks, EnablePublishHooks>`
- `InboundEventBusAdapter<...>`
- `OutboundEventBusAdapter<...>`

Do not collapse these into `EventBus<Direction>` or a direction flag on one
class. Inbound and outbound have different blocking shapes, hook points,
ingress or egress boundaries, and error/reporting paths. `InboundEventBusCore` owns fetch plus
deserialization only and yields `Envelope` or `None`; it does not know agent
handlers or mediator routing rules. `OutboundEventBusCore` owns serialization
plus publish only and consumes `Envelope`; it does not know Python payload
codecs or agent delivery semantics.

Fetch and publish strategies must stay transport-neutral. Kafka, ZeroMQ, smoke
signals, or any other transport are just strategy implementations. ZeroMQ
remains only a phase 2 transport strategy example inside the event bus surface,
not the runtime's semantic model.

### 4.3 Thin adapter, thin public Python, no Python below the edge

The architecture should follow the repo pattern directly:

- C++23 core with no pybind11 dependency
- Thin pybind11 adapter layer
- Thin public Python module in `pygim.ai.runtime`

Agent handlers and tool handlers are Python callables invoked at the adapter edge.
Inbound and outbound event bus adapters own Python hook bridging plus the polling
or publish loops needed to keep transport waits off `MediatorCore`. Inbound
fetch and deserialization submit directly into ingress in v1, while outbound may
keep its own bounded queue before publish.
Any registered agent may optionally bind an `AgentApiStrategy` at that same
adapter edge. That seam covers provider-specific
integrations such as OpenAI, a generic HTTP API, a local model server, or a
subprocess-backed API. The strategy stays behind the agent worker boundary and
adapts provider execution back into the same `handler(ctx, event)` call shape.
Provider-specific request shaping, sessions, retries, and credentials must not
leak into `MediatorCore`.
No `py::object` should exist in the core, in the event bus cores, or in any
transport strategy backend. That line is non-negotiable if the runtime is meant
to stay testable and performant.

### 4.4 Strategy and policy surface

The strategy surface splits into template-level runtime strategies and
runtime-bound per-agent strategies.

Template-level runtime strategies are:

- `CodecStrategy`
- `ToolExecutionStrategy`
- `InboundFetchStrategy`
- `InboundDeserializationStrategy`
- `OutboundSerializationStrategy`
- `OutboundPublishStrategy`

Runtime-bound per-agent strategies are:

- `AgentApiStrategy`

The main policy axes are:

- `EnableHooks` for mediator diagnostics
- `EnableFetchHooks`
- `EnableDeserializationHooks`
- `EnableSerializationHooks`
- `EnablePublishHooks`
- Future overflow policy variants only after v1 proves `RejectNew` semantics

Keep the repo-style split between strategy and policy. Each event bus stage
selects its own `HooksBundle` or `NoStageHooks` specialization through the local
boolean template parameter; `NoHooks` and `NoStageHooks` should compile unused
hook paths away.

`AgentApiStrategy` is distinct from `CodecStrategy`, `ToolExecutionStrategy`,
and the inbound or outbound event-bus transport strategies. It is an adapter-
edge collaborator bound per registered agent, not a template argument on
`AgentSystemAdapter` or `MediatorCore`. Different agents in the same runtime may
use different API strategies simultaneously. This preserves the existing
compile-time branching story for hooks and stage-local features unchanged, and
it keeps the inbound or outbound bus split unchanged because API-provider logic
is not a routing or transport seam.

Typical `AgentApiStrategy` implementations might target OpenAI, a generic HTTP
API, a local model server, or a subprocess-backed API. Those integrations stay
behind the agent worker boundary and return through the same runtime reply and
error rules as any other agent handler.

Hooks are observational only in v1. They may emit diagnostics or metrics, but
they must not mutate fetched transport messages, deserialized envelopes, publish
decisions, routing decisions, recipient selection, queue admission results, or
reply tracking semantics.

### 4.5 Concurrency model

The runtime model for v1 is intentionally conservative:

- One mediator router thread
- One worker thread per agent
- One bounded tool worker pool
- One best-effort diagnostics worker if hooks are enabled

Use `std::jthread` and `stop_token` throughout the design notes. Queue correctness
comes before lock-free optimization. Unbounded queues are rejected.

### 4.6 Request/reply as the common control path

Directed request/reply and tool invocation should share the same `ReplyTracker`.
Tool invocation should not invent a second reply mechanism. A tool call is still a
message exchange mediated by the same router, just with a tool executor on the
execution side instead of another agent mailbox.

### 4.7 Transport scope for v1 and phase 2

ZeroMQ, if added, is phase 2 and only as optional fetch or publish strategies
plus envelope serialization and deserialization strategies inside the adapter-
owned event bus surface. It is not the semantic model for the runtime, and it is
not a reason to switch the core to network-first abstractions.

For phase 2, narrow the scope first:

- Remote directed and request-reply-like delivery first, with adapter-owned
  control-plane policy deciding outbound eligibility
- Local and remote delivery stay mutually exclusive in the first cut
- Distributed broadcast stays local until remote subscription ownership and a
  control plane exist

Do not present a dedicated ZeroMQ extension as a settled repo-ready packaging
commitment. At most, describe it as a possible optional module shape if explicit
build gating and feature-loading are added later.

NNG, Nanomsg, and Boost.Asio should be rejected as defaults for v1. They widen the
surface area before the core routing model is proven and do not buy enough to
justify that complexity up front.

### 4.8 Recommended API shape

The public Python surface should stay small and obvious:

```python
class BroadcastResult:
    delivered_count: int
    dropped_count: int


class RuntimeConfig:
    ingress_capacity: int = 4096
    default_mailbox_capacity: int = 1024
    tool_queue_capacity: int = 1024
    diagnostics_queue_capacity: int = 1024
    outbound_bus_queue_capacity: int = 1024  # phase 2 only


class AgentRuntime:
    def __init__(self, config: RuntimeConfig | None = None): ...
    def register_agent(self, name, handler, *, subscriptions=(), allowed_tools=(), api_strategy=None, override=False): ...
    def register_tool(self, name, handler, *, timeout_ms=None, override=False): ...
    def send(self, recipient, payload): ...
    def request(self, recipient, payload, *, timeout_ms=None): ...
    def publish(self, topic, payload) -> BroadcastResult: ...
    def close(self): ...
```

Here `handler` means a Python callable with the runtime-owned call shape:
`handler(ctx, event)` for agents and `handler(tool_ctx, payload)` for tools.
`_pygim._core.agent_protocols` intentionally does not model those callable
contracts.

If `api_strategy` is provided, it is an adapter-owned collaborator for that one
agent. The worker adapts strategy-backed execution to the same
`handler(ctx, event)` boundary, so the public handler shape, Python protocols,
mediator routing, and event-bus contracts stay unchanged. `api_strategy` does
not replace `CodecStrategy`, `ToolExecutionStrategy`, or any transport strategy.

Handler-side contexts should expose only the operations the mediator already owns:

```python
class AgentContext:
    def send(self, recipient, payload): ...
    def request(self, recipient, payload, *, timeout_ms=None): ...
    def publish(self, topic, payload) -> BroadcastResult: ...
    def call_tool(self, name, payload, *, timeout_ms=None): ...
    def reply(self, payload): ...
```

That API is enough for v1. Anything larger is probably a sign that behavior is
being pushed into Python that should stay in the C++ core or adapter.

### 4.9 Blocking handler liveness rules

The v1 liveness contract is intentionally strict:

- `request()` and `call_tool()` remain available from handlers, but both are
  synchronous from the handler's perspective and may block the calling agent
  worker until reply, admission failure, or timeout.
- Self-request is forbidden in v1 and must be rejected immediately.
- General cyclic waits are unsupported in v1. The runtime does not promise
  deadlock detection or recovery for arbitrary wait cycles.
- Cross-agent blocking from inside handlers is allowed only for acyclic flows.
  Handler authors must keep those flows acyclic.
- A future shared executor or async API may relax these constraints, but v1 does
  not attempt to solve general reentrancy.

### 4.10 Handler call contract and bus wrapper protocols

`_pygim._core.agent_protocols` should stay intentionally small and define only
the Python-facing event-bus wrapper or control-plane contracts:

```python
class InboundEventBus(Protocol):
  def start(self, submit_envelope) -> None: ...
  def close(self) -> None: ...


class OutboundEventBus(Protocol):
  def try_submit(self, envelope) -> object: ...
  def close(self) -> None: ...
```

These Python protocols are not 1:1 mirrors of `InboundEventBusAdapter` or
`OutboundEventBusAdapter` internals; they are wrapper or control-plane
contracts around those adapter-owned collaborators.

Separately, the runtime handler call contract should remain narrow and explicit:

- A registered `api_strategy` may sit behind the worker boundary, but the
  runtime still normalizes agent execution to `handler(ctx, event)`.
- Every registered agent handler receives one `EventMessage` wrapper, regardless
  of directed, request, or broadcast delivery.
- `event.payload` is always the decoded Python payload. `event.kind` and the
  optional `recipient` or `topic` fields describe how the message was routed.
- For `Request` events, a non-`None` return value becomes an implicit reply.
  To send `None` as a real payload, the handler must call `ctx.reply(None)`
  explicitly.
- A handler may emit at most one reply. Returning a value after `ctx.reply(...)`,
  or calling `ctx.reply()` more than once, is a protocol error.
- `ctx.reply()` is valid only while handling a `Request` event. Calling it for directed or broadcast traffic, after the handler returns,
  or after a reply was already emitted must be rejected immediately with
  `RouteError`.
- For `Directed` and `Broadcast` events, handlers should return `None`. Any
  non-`None` return on those paths is a protocol error and should be reported
  through diagnostics rather than treated as a reply.
- Tool handlers stay simpler: `handler(tool_ctx, payload)`. They do not receive
  `EventMessage`.
- Inbound wrappers receive a submit callback and forward deserialized envelopes
  directly into the existing ingress queue. There is no separate inbound local
  queue in v1, so ingress backpressure is the inbound backpressure boundary.

---

## 5. Delivery Semantics

- Directed delivery is exact one-recipient routing. The mediator resolves one
  agent id and enqueues to exactly one mailbox or fails the route.
- Broadcast is mediator-computed fanout to subscribers only. It is not a hidden
  wildcard delivery mode.
- Exact-match subscriptions only in v1. No wildcards, pattern routing, or prefix
  matching.
- Broadcast is per-recipient best effort, not atomic.
- Broadcast order is defined per recipient in mediator enqueue order. There is no
  global total order across all recipients.
- Delivery is at-most-once. The runtime does not retry, replay, or deduplicate
  after enqueue failure, timeout, or shutdown.
- Backpressure is explicit. Ingress, agent mailboxes, diagnostics, and tool queues
  are bounded.
- v1 fixes every queue to `RejectNew` because that preserves request determinism
  and makes overload visible immediately.
- `DropOldest` is deliberately out of scope for v1 and should not be exposed as a
  runtime selection until a later design proves it safe across broadcast and
  reply-tracking semantics.

### 5.1 Admission ordering for `request()` and `call_tool()`

The runtime should use one ordering consistently:

1. Perform any pure validation that does not mutate runtime state. In v1 that
   means tool allowlist validation may happen before id allocation.
2. Reserve `message_id` and compute deadline metadata.
3. Attempt downstream admission.
4. Register the reply waiter only after admission succeeds.

For v1, that means:

- `request()` has no pre-admission validation beyond route lookup, so it reserves
  id and deadline immediately before mailbox admission.
- `call_tool()` validates the allowlist first, then reserves id and deadline, then
  attempts tool-queue admission.
- If downstream admission fails, no waiter is left pending.
- Admission failure is immediate and user-visible. It does not become a timeout.

User-visible failure results are therefore exact:

- Unknown recipient -> `RouteError`
- Directed mailbox full -> `MailboxFull`
- Tool allowlist rejection -> `ToolInvocationError`
- Tool queue full -> `ToolInvocationError`

`request()` and `call_tool()` must raise the admission error immediately in these
cases. They must not wait for timeout machinery to clean up a waiter that should
not have existed.

### 5.2 Broadcast overload semantics

`publish()` returns `BroadcastResult(delivered_count, dropped_count)`.

v1 broadcast semantics are deliberately explicit:

- Broadcast fanout is evaluated recipient by recipient.
- Under `RejectNew`, saturated recipients are skipped and counted in
  `dropped_count`.
- Fanout continues for other recipients after one recipient is dropped.
- v1 does not expose `DropOldest` on any queue. If a later design reintroduces it,
  it must remain outside the `publish()` contract until broadcast semantics are
  re-specified.
- Recipient-level diagnostics are best-effort. The API returns aggregate counts,
  while detailed drop events go through diagnostics and hooks.

An empty subscriber set is still a valid broadcast result:

- `delivered_count = 0`
- `dropped_count = 0`

---

## 6. Tool Model

Tools are adapter-edge Python callables described by `ToolDescriptor` metadata and
invoked through a separate executor path.

- Each agent has a tool allowlist. The mediator or adapter must reject tool calls
  that are not explicitly allowed for the caller.
- Tool execution uses a separate bounded executor pool so Python tools do not block
  mediator routing. `MediatorCore` only admits `ToolRequest` work into the tool
  queue; the adapter-owned executor drains that queue and returns `ToolReply`
  through the adapter boundary.
- Tool invocation reuses request/reply semantics through the mediator and the tool
  executor. The reply path still goes through `ReplyTracker`.
- `ToolContext` should carry the caller id, message id, deadline, and diagnostic
  metadata, but not direct access to routing internals.
- `call_tool()` follows the same admission-before-waiter rule as `request()`.
  Allowlist rejection and tool-queue saturation raise immediately, not as timeout.
- Python tool timeouts are soft unless tools move to subprocesses. In-process
  Python cannot be force-cancelled safely, so timeout means the waiting side is
  released and any late reply is dropped.
- Tool exceptions should become structured error replies plus diagnostics. They are
  not a reason to crash the mediator.
- `call_tool()` shares the same blocking and liveness constraints as `request()`.
  v1 intentionally does not solve general reentrancy.

### 6.1 Tool timeout resolution

Tool timeout precedence must be explicit:

1. If `call_tool(timeout_ms=...)` supplies a value, that value wins.
2. Otherwise the runtime uses the registered default from `ToolDescriptor.timeout`.
3. If neither is supplied, v1 uses a runtime default of `30_s`.

The adapter converts any Python `timeout_ms` override to `Duration` once on
entry. The effective deadline is then computed once at admission time and stored
on the submitted envelope as monotonic deadline metadata. The tool executor and reply
tracker consume that resolved deadline; they do not recompute timeout precedence
later.

Examples:

- Registered default `5_s`, no per-call override -> effective timeout `5_s`
- Registered default `5_s`, per-call `1200` ms at the Python edge -> effective timeout `1200_ms`
- No registered default, no per-call override -> effective timeout `30_s`

---

## 7. Runtime Model

The runtime should be built around a small number of stable moving parts:

1. `AgentSystemAdapter` owns the mediator, codec, diagnostics dispatcher, agent
  workers, tool executor, and any optional `InboundEventBusAdapter<...>` and
  `OutboundEventBusAdapter<...>` collaborators.
2. Public API calls encode payloads and submit `Envelope` instances to a bounded
  ingress queue.
3. `InboundEventBusAdapter<...>`, if configured, runs its own polling loop, uses
  `InboundEventBusCore<...>` to execute fetch then deserialization, and submits
  the resulting `Envelope` instances directly into the same ingress queue.
  There is no separate inbound local queue in v1, so ingress backpressure is
  the inbound backpressure boundary.
4. `MediatorCore` runs on one router thread, performs a timed wait based on the
  nearest pending reply deadline, wakes on new ingress or deadline expiry, and on
  wake advances `ReplyTracker` expiration before routing the next admitted work.
5. Each `PyAgentWorker` owns one Python handler and one mailbox drain loop.
6. `ThreadPoolToolExecutor` drains the tool queue and invokes `PyToolBridge` on a
  bounded worker pool.
7. `OutboundEventBusAdapter<...>`, if configured, drains its own bounded queue,
  uses `OutboundEventBusCore<...>` to serialize then publish, and keeps
  transport waits off the mediator thread. Adapter-owned control-plane policy,
  not `MediatorCore` or the bus cores, decides which envelopes are eligible for
  outbound submission.
8. Hook and diagnostic events are emitted best-effort into a bounded diagnostics
  queue and handled off the mediator hot path.
9. Replies from agents and tools re-enter the mediator as `Reply` or `ToolReply`
  envelopes and complete the tracked waiter.
10. Shutdown is cooperative: `close()` rejects new work first, closes queues to
   wake blocked loops, resolves pending waiters with `ShuttingDownError`, and
   then joins long-lived threads via `stop_token`.

`_pygim._core.agent_protocols` should define only the thin inbound and outbound
bus wrapper or control-plane contracts used by the adapter and the thin public
module. It is intentionally not a 1:1 mirror of the loop-owning C++ bus
adapter types. `pygim.ai.runtime` should remain a re-export and ergonomics
layer, not a second implementation.

Core control flow should use `std::expected`-like results for routing, queue,
tool-dispatch, timeout, and shutdown failures. These are expected runtime
conditions, not all exceptional control flow.

### 7.1 Timeout driving

Timeout advancement must not depend on continuous ingress traffic.

The concrete v1 mechanism is:

- `ReplyTracker` exposes the nearest pending deadline, or equivalent next-deadline
  query semantics.
- The mediator router thread performs a timed wait using the earlier of incoming
  ingress activity or the nearest pending deadline.
- On wake, the router calls `ReplyTracker.expire_ready(now)` before routing the
  next admitted unit of work.

A small deadline heap is the obvious implementation shape, but the design only
requires equivalent next-deadline query behavior. The important part is that idle
ingress does not stall timeout expiry.

### 7.2 Runtime lifecycle and close semantics

The runtime lifecycle should stay small and explicit:

- `Created` is a short construction state used while the runtime is being
  assembled.
- `Running` means the mediator thread, agent workers, tool workers, and optional
  diagnostics worker are active. v1 enters `Running` eagerly when the runtime is
  created; there is no lazy start on first send.
- `Closing` begins when `close()` is called. New registration, send, request,
  publish, and tool submission operations are rejected with `ShuttingDownError`.
- `Closed` means queues are closed, threads are joined, and no further operations
  are allowed.

Registration rules are intentionally simple:

- Agent and tool registration are allowed only while `Running`.
- Registration takes effect immediately for subsequent routed work.
- Registration during `Closing` or `Closed` is rejected.

### 7.3 Registration contract

Registration should mirror the strict override semantics already used elsewhere in
pygim:

- Agent ids and tool names are unique keys.
- `register_agent(..., override=False)` and `register_tool(..., override=False)`
  reject duplicate names.
- `override=True` replaces the existing handler and registration metadata
  atomically, and it is valid only if the name already exists.
- `override=True` on a missing name is a registration error. It must not create
  a new entry.
- Agent override replaces the handler, optional per-agent `api_strategy`,
  subscriptions, and allowlists as one unit. Tool override replaces the handler
  and descriptor metadata, including the default timeout.
- Registration is adapter-owned control-plane work. `AgentSystemAdapter`
  coordinates Python handler swap, optional per-agent strategy swap, and
  mediator slot installation under one control-plane lock; routing never
  returns `RegistrationError`.
- Active agent override is intentionally strict in v1: it succeeds only when the
  current mailbox is empty and no handler invocation is in flight. Otherwise the
  override is rejected with `RegistrationError`.
- Active tool override is equally strict in v1: it succeeds only when no queued
  or in-flight `ToolRequest` exists for that tool. Otherwise the override is
  rejected with `RegistrationError`.
- Successful cutover makes the new handler, optional per-agent `api_strategy`,
  subscriptions, allowlist, callable, and descriptor metadata visible
  atomically at install time. No mailbox or tool queue migration occurs in v1.
- v1 does not support incremental subscription mutation after registration.
  Subscriptions change only through `register_agent(..., override=True)`.
- Duplicate registration or invalid override requests raise `RegistrationError`
  on the Python surface. They are not routed and do not emit synthetic messages.
- Tool cutover is enforced by an adapter-owned tool registry entry that stores the
  callable, descriptor, queued count, and in-flight count for each tool under the
  same control-plane lock.

`close()` semantics are also explicit:

- Stop accepting new ingress and new registrations first.
- Close ingress, tool, and diagnostics queues so blocked loops wake promptly.
- Complete unresolved request and tool waiters with `ShuttingDownError` rather
  than leaving callers blocked until timeout.
- Allow already running handlers and tools to finish best-effort.
- Drop queued but not yet started envelopes immediately with diagnostics once
  `Closing` begins.
- Any reply that arrives after its waiter was resolved as `ShuttingDownError` is
  a late reply and is dropped through the normal diagnostics path.
- Join all runtime threads and then transition to `Closed`.

### 7.4 RuntimeConfig surface

The v1 runtime should expose a compact configuration surface instead of scattering
queue sizes across unrelated calls. A single `RuntimeConfig` is enough:

All v1 capacities must be integers greater than or equal to 1. Invalid values
raise `ValueError` during `RuntimeConfig` construction, before any runtime
threads or queues are created.

| Setting | Scope | Default | Notes |
| ------- | ----- | ------- | ----- |
| `ingress_capacity` | per runtime | `4096` | Bounded ingress queue for all public submissions |
| `default_mailbox_capacity` | per agent | `1024` | Applied to every agent in v1; per-agent override is out of scope |
| `tool_queue_capacity` | per runtime | `1024` | Shared queue for all tool requests |
| `diagnostics_queue_capacity` | per runtime | `1024` | Best-effort diagnostics and hook events |
| `outbound_bus_queue_capacity` | per runtime | `1024` | Phase 2 only; local buffer for the outbound publish side when configured |

The public constructor or factory should accept one optional `RuntimeConfig`. v1
does not expose per-tool queue sizes or per-agent mailbox overrides. Inbound
fetch and deserialization do not have a separate local capacity knob in v1
because they submit directly into ingress.

---

## 8. Failure Semantics

| Failure | Behavior | Notes |
| ------- | -------- | ----- |
| Unknown recipient | Immediate `RouteError` | Directed delivery never silently broadcasts or retries |
| Unknown topic subscribers | `BroadcastResult(0, 0)` plus optional diagnostic | Empty fanout is valid and still at-most-once |
| Directed mailbox full | Immediate `MailboxFull` | Admission fails before waiter registration |
| Broadcast recipient full under `RejectNew` | Recipient skipped, `dropped_count += 1` | Fanout continues for remaining recipients |
| Tool not allowlisted | Immediate `ToolInvocationError` | Checked before tool execution enters the worker pool |
| Tool queue full | Immediate `ToolInvocationError` | No waiter is created |
| Self-request | Immediate `RouteError` | Forbidden in v1 |
| Cyclic blocking across agents | Unsupported | Handler authors must keep blocking flows acyclic; v1 does not promise general deadlock handling |
| Reply timeout | `ReplyTracker` expires waiter on router timed wake | Late replies after timeout are dropped and surfaced through diagnostics |
| Tool exception | Structured error reply | Mediator stays alive; failure is reported through the reply path |
| Diagnostics queue full | Hook notification dropped and counters incremented | Routing path stays non-blocking |
| Hook callback failure | Diagnostic only | Hooks are observational only in v1 and must not alter routing |
| Duplicate agent or tool registration | Immediate `RegistrationError` | Default registration rejects duplicates; `override=True` requires an existing entry |
| Shutdown in progress | New ingress rejected | Existing work drains best-effort under stop policy |
| Phase 2 inbound fetch or outbound publish overload (`EAGAIN`, HWM saturation) | Drop or refuse transport transfer plus diagnostic | Transport loops must not block the mediator |

Admission failure for `request()` and `call_tool()` raises immediately as
`RouteError`, `MailboxFull`, or `ToolInvocationError`. It must not surface as a
timeout because waiter creation happens after successful admission.

### 8.1 Hook operational contract

Hooks are observational only. In v1, the mediator hot path should emit small
diagnostic events into a bounded diagnostics queue, and hook callbacks should run
best-effort on a diagnostics worker rather than inline on the router thread.

| Hook point | Execution thread | Sync vs async | Blocking rule | Failure behavior | Overload behavior |
| ---------- | ---------------- | ------------- | ------------- | ---------------- | ----------------- |
| Route accepted | Diagnostics worker | Async best-effort | Never block the mediator; router only attempts non-blocking diagnostics enqueue | Callback failures are swallowed and counted | Notification may be dropped if diagnostics queue is full |
| Route rejected or dropped | Diagnostics worker | Async best-effort | Same rule; no blocking inline on admission path | Callback failures are swallowed and counted | Drop notification and increment counters on diagnostics overload |
| Reply timeout | Diagnostics worker | Async best-effort | Timeout expiry must remain driven by router timed wake, not hook latency | Callback failures are swallowed and counted | Drop notification and increment counters on diagnostics overload |
| Tool result or tool error | Diagnostics worker | Async best-effort | Tool completion must not wait on hook work | Callback failures are swallowed and counted | Drop notification and increment counters on diagnostics overload |
| Phase 2 inbound fetch or outbound publish overload | Diagnostics worker | Async best-effort | Transport overload reporting must not block mediator or bus I/O loops | Callback failures are swallowed and counted | Drop notification and increment counters on diagnostics overload |

If `EnableHooks` is disabled, this entire path should compile out or collapse to a
zero-cost no-op.

### 8.2 Reply-path failure contract

Reply handling needs one explicit rule set so callers do not guess:

- At the adapter boundary, decoded reply envelopes become `ReplyOutcome`: either
  success payload or structured error metadata before the runtime chooses return
  versus exception.
- Success replies carry decoded payload only. Error replies carry structured
  fields: `code`, `message`, `source`, and `retryable`.
- `request()` raises `RouteError` when it receives an agent-side error reply.
  `call_tool()` raises `ToolInvocationError` when it receives a tool-side error
  reply. The caller does not receive a decoded error object on the success path.
- For `Request` events, a non-`None` return value becomes the implicit reply.
  Returning `None` without an explicit `ctx.reply(...)` is a `missing_reply`
  protocol error.
- `ctx.reply(None)` is the explicit way to send `None` as a valid reply payload.
- If an agent handler raises while serving a `Request`, the adapter emits a
  structured `handler_exception` reply plus diagnostics; the requester then sees
  `RouteError`. If the same handler raises on a directed or broadcast event, the
  runtime emits diagnostics only because no reply path exists.
- If a tool handler raises, the tool executor emits a structured `tool_exception`
  reply plus diagnostics; the caller then sees `ToolInvocationError`.
- If a handler emits more than one reply, the first admitted reply wins. Later or
  duplicate replies are dropped and surfaced through diagnostics only.
- Late replies after timeout or after `Closing` begins are dropped and surfaced
  through diagnostics only.

---

## 9. Phase 2 Distributed Event Bus Transport

Phase 2 may add optional ZeroMQ fetch and publish strategies in C++ using libzmq,
cppzmq, or the raw C API. It should not use pyzmq. This remains conceptual unless
the repo later adds explicit build gating and feature-loading semantics. A future
optional module could take a shape such as `ext.agent_runtime_zmq.toml`, but this
document does not treat that as a settled packaging commitment.

Key constraints for phase 2:

- The semantic center remains the in-process mediator.
- `InboundEventBusAdapter<...>` and `OutboundEventBusAdapter<...>` remain
  adapter-owned. `MediatorCore` does not own or depend on transport fetch,
  publish, serialization, or deserialization strategies.
- Remote directed and request-reply-like delivery come first. Distributed
  broadcast stays out of scope until remote subscription ownership and
  control-plane rules exist.
- The transport backpressure boundary must be explicit: inbound fetch and
  deserialization submit directly into ingress, so ingress saturation is the
  inbound refusal boundary; outbound may keep its own bounded queue before
  publish.
- `EAGAIN`, HWM saturation, or any other transport overload condition maps to
  runtime diagnostics and message drop or refusal semantics. It must not block
  the local mediator thread.
- Use `ROUTER/DEALER` semantics for remote directed delivery and remote request
  paths.
- Do not use `PUB/SUB` as the semantic model for broadcast. Broadcast remains
  mediator-computed fanout to subscribers only, even if later forwarded across an
  outbound transport.

### 9.1 Egress policy

Outbound eligibility is adapter-owned control-plane work. `MediatorCore` routes
local envelopes, and the inbound or outbound bus cores only perform fetch,
deserialize, serialize, and publish stages. None of those types decide whether a
message is eligible for remote egress.

The first phase-2 cut should stay strict:

- Only remote directed and request-reply-like traffic is eligible for outbound
  submission.
- Local and remote delivery are mutually exclusive for a given envelope in the
  first cut.
- Broadcast stays local until a later control-plane and subscription model
  exists.
- Exact-match subscriptions remain the only subscription model unless a later phase
  proves that broader matching is worth the complexity.
- Backpressure, at-most-once delivery, timeout behavior, and reply tracking remain
  defined by the local mediator, not by ZeroMQ socket behavior.

If the inbound and outbound event buses are absent, the architecture is still
complete. That is the point of the design: prove the in-process runtime first,
then add optional transport strategies without rewriting the core.
