# Architecture simplification proposal

Use the [RobustToolbox ECS architecture](https://docs.spacestation14.com/en/robust-toolbox/ecs.html) as a reference,
particularly its data-only components, public system capabilities, explicit system ordering, and immediate event
dispatch. UnrealVoxelSim retains its own module boundaries and uses interfaces rather than dependencies on concrete
system classes.

## Execution model

Authoritative simulation state is thread-affine and belongs to one simulation execution context. Ticks and all calls
that may mutate authoritative state execute synchronously on that context. Concurrent simulation mutation is forbidden.

The execution model is the same for every caller. Qt, Unreal Engine, tests, tools, and simulation systems call their
injected API interfaces directly. The called implementation neither knows nor cares which kind of caller initiated the
call. Composition controls which capabilities each caller receives.

When a frontend and the simulation share a thread, the host event loop naturally serializes their calls. A normal Qt
event cannot interrupt a synchronous simulation call; it is delivered after the current call returns. Simulation code
must not pump a nested platform event loop while mutating simulation state. A callback originating on another thread
must be marshalled to the owning simulation execution context before it calls a simulation API.

Calls execute immediately. They may occur between ticks or synchronously within a tick when one system calls another.
The actual call order, including nested calls and synchronous event handlers, is the deterministic execution order.
Scheduled tick participants still have an explicit deterministic order defined by composition.

Reentrant calls are allowed by default. A system whose invariants do not support reentry must reject it with a
system-wide execution guard. Such a guard covers the complete system instance rather than an individual method, so a
dynamic call cycle such as `A -> B -> A` is detected when it re-enters `A`. No central proxy or dispatcher is required
solely to detect arbitrary dynamic call cycles. Static module dependency cycles remain forbidden.

## Commands and domain APIs

1. Remove tick-stamped command DTOs, command variants, command sinks, command processors, and per-domain command queues
   from ordinary in-process interaction.
2. Express systems' capabilities as narrow dependency-injected interfaces defined in the owning `*.Api` module.
   State-changing interface methods are still commands in the command-query-separation sense; this proposal removes
   the queued command-message infrastructure, not the semantic distinction between commands and queries.
3. A production frontend normally receives high-level player/application input APIs. Development frontends, testbeds,
   and tests may receive lower-level domain APIs when useful. Both cases use the same synchronous interaction model.
4. Lifecycle and scheduled-update capabilities should be exposed only to composition unless another system explicitly
   needs them. Ordinary callers receive only the domain capabilities they are authorized to invoke.
5. If replay, networking, or input auditing later requires a durable ordered input stream, implement it once at the
   application edge. Do not recreate a command queue independently inside every domain.

## Events

Remove event buffers and generic event-pump phases. Local simulation events are delivered immediately and synchronously,
using the immediate-event behavior of RobustToolbox as the reference model.

- Events announce facts or conduct an explicitly documented cooperative interaction. Direct API calls request required
  behavior.
- Concrete event types are allowed and are defined in the producer's `*.Api` module. Consumers depend on the event
  contract and source interface, not on the producer's concrete implementation.
- Raising an event invokes its applicable handlers inline before returning to the publisher.
- Nested event publication is allowed and is depth-first: a nested event completes before the current handler resumes.
- Handler order is deterministic. The default is subscription order. An event whose semantics require another order
  must define that order explicitly and cover it with tests.
- Dispatch snapshots the applicable recipients before invoking them when handler activity could otherwise invalidate
  iteration. Recipient validity is checked before invocation when an earlier handler can destroy that recipient.
- Subscriptions are normally configured during construction or initialization. Subscription mutation during dispatch
  is forbidden. Broader runtime subscription mutation may be added only with explicitly defined deterministic
  semantics.
- Event handlers are `noexcept`. Recoverable failures are handled inside the listener's domain boundary.
- An event is raised only when the producer has completed the mutation represented by the event and its externally
  observable invariants hold.
- Systems that cannot tolerate reentry use the same system-wide execution guard as direct API methods.

Queued delivery may be introduced later for a concrete requirement, such as a cross-thread or network boundary, but it
is not the default local inter-system interaction model.

## Simulation inputs and determinism

Calls originating outside the simulation pipeline, such as Qt or Unreal callbacks, are external simulation inputs.
Their ordering relative to simulation ticks and other external calls is part of the input sequence. Given the same
initial state and the same ordered input/tick sequence, execution must be deterministic.

This is valid:

```text
Qt -> IVoxelEditing::Remove(...)
Tick
Qt -> IEntitySpawning::Spawn(...)
Qt -> ISelection::Set(...)
Tick
Tick
```

This is not:

```text
Tick || Qt -> IVoxelEditing::Remove(...) // concurrent mutation
```

A direct call made synchronously by a tick participant is part of that tick's call sequence and does not need to be
converted into an external input or delayed until a later phase.

## Composition

Introduce a `Composition.Game` module that constructs the simulation session and scheduled pipeline from concrete
implementation modules. It is the platform-independent application-level composition root.

The low-level executable entry point selects platform implementations and supplies only their portable API interfaces
to `Composition.Game`. For example:

```text
Qt main()
 |- constructs QtFileSystem : IFileSystem
 |- constructs QtInput ...
 `- constructs Composition.Game(
        IFileSystem&,
        ...
    )

Composition.Game
 |- constructs Voxel.Chunked
 |- constructs Movement.Voxel
 |- constructs Navigation...
 `- constructs the ordered simulation pipeline
```

`Composition.Game` is allowed to depend on concrete modules such as `Movement.Voxel` and `Voxel.Chunked` precisely
because it is a composition root. It owns their lifetimes and destruction order.

`Composition.Game` exposes API views required by application-edge adapters such as Qt and Unreal Engine. It must never
be injected into a game/domain system; doing so would turn it into a service locator. Domain systems receive only their
explicit API dependencies.

The composition root defines the deterministic order of scheduled simulation participants. Systems are not required to
be agnostic to that order, but every behavior-affecting ordering dependency must be explicit and tested.

## Components and ECS authority

Components remain data-only structs. Domain behavior belongs in systems rather than component methods. Harmless
language-level operations such as defaulted construction or comparison do not make a component behavioral.

Components defined in a public `*.Api` module are public read contracts and may appear in another module's ECS
permission declaration. Merely depending on the API module does not grant registry access. Actual read, existing-value
write, structural, and entity-lifecycle authority remains explicitly granted by composition through the existing ECS
permission system.

Mutation is reserved to the owning domain unless its API explicitly grants another domain write authority.
Implementation-private components may remain in implementation modules and are not made public merely for persistence.

## Persistence

Start with a minimal component-oriented snapshot design. Do not introduce a universal domain snapshot framework,
runtime reflection system, or one abstraction layer per persistence concern without a concrete need.

Prefer authoritative entity state in persistable components. Keep systems stateless or reconstructible whenever
practical. Genuinely non-ECS authoritative stores, such as the voxel world, provide their own section serializer and
deserializer appropriate to their storage model.

The module that defines the persistence semantics of a component or non-ECS store provides its codec. Serialization
concerns do not become component methods, and runtime C++ object layout is not itself the save format. A component codec
may initially map one-to-one to its component and serialize its fields directly. It may later translate or omit fields
when runtime-only or reconstructible state requires it.

### Minimal snapshot format

A V1 snapshot contains:

1. A header containing a save-format version and behavior-affecting global state such as the simulation tick.
2. An entity table assigning a snapshot-local entity ID to every persisted entity.
3. One independently identified and versioned section for each persisted component type. Each row contains a
   snapshot-local entity ID and that component's serialized data.
4. Independently identified and versioned custom sections for authoritative non-ECS stores such as voxel chunks.

The set of persisted component types and custom sections may initially be registered explicitly in `Composition.Game`.
There is no requirement for automatic component discovery.

### Entity references

Runtime `EntityId` values are never persisted directly. During capture, all serialized entity and component references
are translated through the same runtime-to-snapshot mapping. During restoration, snapshot-local references are
translated through the corresponding snapshot-to-runtime mapping.

Restoration proceeds in deterministic passes:

1. Create a fresh, inactive simulation session with an empty ECS registry.
2. Create all persisted entities and construct the snapshot-to-runtime entity mapping.
3. Deserialize component sections and resolve their entity references through that mapping.
4. Restore custom authoritative stores.
5. Rebuild required reconstructible state and validate the session.
6. Install the restored session only after all preceding steps succeed.

A failed restoration leaves the current active session unchanged. For the first format version, unsupported section
versions may fail cleanly; migration machinery is added when a second schema version creates a real requirement.

### Capture boundary

Capture is a synchronous top-level operation on the owning simulation execution context. It begins when no other API
call or tick is active, so it observes a coherent call boundary and no authoritative mutation is concurrent with it. A
save requested from inside a tick is performed after that tick returns. Immediate events leave no pending local event
buffer to persist. Behavior-affecting future intentions, random state, or incremental work that must survive loading
are authoritative state and must be represented in a persisted component or custom section. Reconstructible caches
and presentation state are not persisted.
