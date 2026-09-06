# Generic Job System Concept

## Purpose

The job system is a domain API for representing, allocating, executing, and observing work in the simulation.

It provides a common language between independent systems:

- a system that requests work;
- a system that decomposes work into executable steps;
- a system that decides which worker should perform work;
- a system that executes an assigned step;
- a system that observes progress and reacts to results.

The job system does not define pawns, factions, settlements, AI, capabilities, navigation, inventory, resources, voxels, workstations, combat, or concrete jobs. Those belong to other domains and integrate through adapters.

The system supports simple requests such as “perform this action” and long-running plans such as “construct this building,” while remaining unaware of the meaning of either request.

## Design goals

The API should:

1. represent work without prescribing its subject matter;
2. allow any number of independent issuers, planners, allocators, and executors;
3. support hierarchical controllers without requiring a fixed hierarchy;
4. distinguish a high-level job from the primitive work needed to fulfill it;
5. allow plans to be generated incrementally and revised while running;
6. prevent conflicting workers from executing the same exclusive work;
7. express prerequisites, produced facts, consumed inputs, and synchronization points;
8. tolerate workers becoming unavailable, work becoming invalid, and the world changing;
9. expose stable identifiers and event history for persistence, replay, debugging, and UI;
10. permit new worker types, controllers, capabilities, jobs, and execution strategies without changing the core API.

The API should not assume that every job is autonomous, that every worker is a pawn, that every job has one worker, or that every plan is known in advance.

## Terms

A **work request** asks that some outcome be achieved. It is submitted by an issuer and may be satisfied by one job, a plan, or a chain of plans.

A **job** is a unit of work with a lifecycle, requirements, and intended outcome. It may be high-level or directly executable.

A **plan** is a graph or sequence of jobs and dependencies used to fulfill a request. A plan is data and policy owned by the planning system, not by the generic job runtime.

A **step** is an executable part of a job. It may be a primitive action or may itself be delegated to another job system.

An **order** is an execution instruction delivered to a worker. It is a runtime projection of an assigned step and may be retried, interrupted, or replaced.

A **worker** is any entity or external executor that can accept and perform orders. The API treats workers by identity and reported properties only.

A **capability** is a declarative description of work a worker or executor can perform. The job system matches requirements to capabilities but does not define capability semantics.

A **controller** is any system that creates requests, plans work, allocates workers, executes work, or reacts to events. The API does not reserve these responsibilities for a particular controller type.

A **resource** is any identity that can be required, reserved, consumed, produced, locked, or affected by work. It may represent an item, location, entity, reservation domain, or abstract fact.

## Conceptual boundaries

The generic layer has five separable responsibilities.

### Requesting

An issuer creates a work request with an outcome description, priority, policy metadata, and optional ownership or cancellation information. The issuer may be a player command, a pawn-needs controller, a settlement controller, a planner, or another job system.

### Planning

A planner interprets a request and produces a job or plan. Planning may be eager, lazy, incremental, hierarchical, or delegated. The runtime must not require a planner: a caller may submit a directly executable job.

### Allocation

An allocator selects an eligible worker or workers for an available step. Allocation uses worker identity, capability descriptors, requirements, reservations, priority, and policy supplied by the caller or an allocation service. It must support zero, one, or multiple workers.

### Execution

An executor turns an assignment into orders and reports progress and terminal results. Execution belongs to the worker integration, not the job domain. A worker can execute a sequence, pause, reject, fail, partially complete, or report that work is no longer valid.

### Observation

The runtime records state transitions and publishes events. Controllers can observe events, query state, create follow-up work, replan, or cancel work. Observers do not need to be the issuer or allocator.

## Core model

The minimum persistent model should contain:

- `WorkRequestId`, `JobId`, `PlanId`, `StepId`, and `AssignmentId`;
- `WorkerId` and capability identifiers;
- lifecycle state;
- priority and scheduling metadata;
- requirements and constraints;
- dependencies;
- reservation references;
- timestamps or simulation ticks;
- version or revision;
- correlation and parent identifiers;
- terminal outcome and failure reason.

Identifiers are opaque value types. No consumer should infer meaning from their representation.

A job should have a stable identity across retries and reassignment. An attempt, assignment, and order should have separate identities so that history remains distinguishable.

A job may have zero or more child jobs, steps, dependencies, participants, and affected resources. These relationships must not imply a tree: shared prerequisites and coordination between branches require a directed acyclic graph or equivalent dependency model.

## Requirements and capabilities

A job declares requirements. A worker or executor declares capabilities. Matching uses opaque, extensible descriptors.

A requirement can describe required capability, quantity or level, worker count, worker role, location or access constraint, tags, equipment or inputs, world facts, exclusions, deadlines, and interruption policy. It should also state whether the condition must hold at allocation, order acceptance, and execution time.

The core API should provide structured fields for common matching and an extension mechanism for domain-specific predicates. It must not interpret “can mine stone,” “has hands,” or “belongs to faction X.”

Capabilities are an eligibility declaration, not a guarantee of success. A worker may have a capability but reject an order because its state, equipment, location, or local world facts changed.

## Plans, steps, and sequencing

A plan is an explicit dependency graph. Dependencies may express completion order, milestone waits, produced facts, mutual exclusion, alternatives, conditional branches, result propagation, or invalidation.

The core should support ordered sequences, parallel steps, joins, conditional branches, alternatives, cancellation propagation, retry policy, compensation or cleanup steps, milestones, and partial completion.

A plan should not require every step to be materialized at creation time. Planners may add, replace, or retire steps as information changes, subject to revision rules and event history.

The distinction between a job, its steps, and individual execution attempts should remain visible even when a job exposes a direct sequence of orders.

## Allocation and reservations

Allocation creates an assignment with an explicit lease or validity period. It records the selected worker or group, step, evaluated requirements, reservation claims, allocation policy, expiration, renewal data, and assignment state.

Reservations are claims over opaque resources or domains. They prevent incompatible work from being allocated concurrently when the integrating domain says they conflict. The API should support exclusive and shared reservations, quantities, ordered acquisition, expiration, release, conflict reporting, and revalidation.

Reservation semantics come from a provider or domain adapter. The generic runtime must not know whether a resource is a voxel, item stack, workstation, target entity, or abstract construction slot.

Reservation ownership must be visible in state and events. Reassignment must produce an explicit transition.

## Execution protocol

An executor should be able to:

1. accept or reject an assignment;
2. begin an attempt;
3. report progress or milestones;
4. report interruption, suspension, or inability to continue;
5. report success, partial success, or failure;
6. release or renew reservations as directed by policy.

The protocol must support idempotent commands and duplicate event delivery. Repeating a command after an uncertain connection state must not create a second logical execution.

Execution reports should include structured result and failure payloads. Diagnostic text may be included, but callers must not parse text to make decisions.

## Lifecycle

The lifecycle should separate intent, availability, ownership, execution, and outcome:

```text
Draft -> Submitted -> Planned -> Available -> Assigned -> Accepted -> Running
  -> Suspended -> Available
  -> Succeeded | PartiallySucceeded | Failed | Cancelled | Expired | Invalidated
```

Not every implementation must expose every state, but transitions must be explicit and validated. `Suspended` means work may continue later. `Failed` means the current contract or attempt could not be completed. `Invalidated` means its assumptions or target are no longer valid. `Cancelled` means authorized policy ended it.

Retries create a new attempt and preserve job identity. A retry may preserve or reacquire reservations according to policy. Terminal states should be immutable except for annotations or a separate superseding relationship.

## Failure and replanning

Failure is normal and machine-readable. Extensible categories should include unavailable worker, capability mismatch, reservation conflict, missing input, invalid target, unreachable target, changed world, blocked dependency, interruption, executor rejection, timeout, policy denial, and external failure.

A failure may be retryable, retryable after a condition, recoverable by replanning, terminal for the job, or terminal for its parent plan. The generic layer reports facts and preserves history; a planner or controller decides whether to retry, replace, compensate, escalate, or abandon.

## Coordination between controllers

Multiple controllers may submit work for the same worker, resource, or objective. Coordination uses explicit priorities, arbitration metadata, reservation providers, authority scopes, leases, cancellation tokens, superseding relationships, conflict events, and deterministic tie-break data.

A lower-level controller may submit a request whose fulfillment is delegated to a higher-level controller, and a higher-level controller may submit child requests to lower-level controllers. This is represented by opaque parent and delegation identifiers. The core must not enforce a fixed direction such as “AI plans, pawns execute.”

## Persistence, events, and queries

The API should be serializable without serializing concrete workers or world objects. Active assignments and reservations must be revalidated after loading.

Every meaningful state change should be a typed event containing event identity, sequence or version, simulation time, affected identifiers, source, structured payload, and correlation and causation identifiers. Consumers should resume from a cursor and safely process duplicates.

Queries should cover jobs by state or parent, available steps by requirements, assignments by worker, reservations by resource, plan milestones, failures and retries, and changes since a revision.

## Extensibility and module boundaries

New capability, job kind, requirement predicate, result type, failure code, reservation domain, or executor extension should use registered identifiers and payload schemas without changing generic interfaces. Unknown extensions should be preserved when possible and rejected clearly when they cannot be interpreted.

Possible `.Api` boundaries are:

- `JobsConcept.Api`: identifiers, lifecycle, requests, jobs, steps, plans, dependencies, assignments, attempts, results, failures, and events;
- `JobsRequirements.Api`: capability descriptors, requirement descriptors, matching contracts, and evaluation results;
- `JobsCoordination.Api`: allocation, worker registry views, leases, reservations, arbitration metadata, and authority scopes;
- `JobsExecution.Api`: orders, executor interfaces, execution reports, interruption, retry, and idempotency;
- `JobsPersistence.Api`: serialization, snapshots, event cursors, restoration, and migration metadata.

A first version may keep these in one `Jobs.Api` module if dependencies remain directed. No `.Api` module should depend on Unreal Actor or pawn classes, voxel or navigation implementations, inventory implementations, concrete AI controllers, or concrete job definitions.

## Non-goals

The generic job system does not choose objectives, discover construction methods, navigate, implement mining, chopping, hauling, combat, crafting, or construction, own behavior trees or utility scoring, define pawns or resources, guarantee capability success, replace domain planning, require one global scheduler, require one controller per worker, or require every job to use a physical worker.

## Initial acceptance criteria

The first generic implementation is adequate when it can:

1. create and identify a request, job, plan, step, assignment, attempt, and order;
2. represent a direct step and a multi-step dependency graph;
3. match opaque worker capabilities against opaque requirements;
4. assign one worker, multiple workers, or an external executor;
5. reserve an opaque resource with a lease and report conflicts;
6. handle acceptance, progress, interruption, retry, success, failure, cancellation, and invalidation;
7. preserve parent-child and delegation relationships;
8. publish and replay state transitions;
9. serialize and restore active work with revalidation;
10. add a new capability and job payload without changing generic interfaces.

## Open design questions

- Should plans be owned by the runtime or by planners, with the runtime storing graph state and references?
- Should requirements be declarative data only, or may they contain executable predicates?
- Is reservation atomicity provided by one global coordinator or by pluggable domains?
- Are orders durable domain objects, transient executor messages, or both?
- Which transitions are fixed by the API and which are host policy?
- Does partial success belong to an attempt, a job, or only a result payload?
- How should progress be represented when steps have incomparable units?
- How are worker groups formed and replaced?
- What consistency is required when controllers submit competing work in one simulation tick?
- Which events are authoritative for persistence and replay?
- How much validation belongs in the API versus adapters?
- Is compensation a first-class relation or another dependent step?
