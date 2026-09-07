# Generic Job System Concept

## Purpose

The job system provides shared contracts for concrete work, dependency execution, worker allocation, resource allocation, and results in the simulation.

All gameplay control is performed by AI. Human players do not directly control countries or pawns. AI controllers choose objectives and generate work; the job runtime coordinates the concrete jobs needed to carry it out. Controllers may delegate to one another without a fixed country, settlement, or pawn hierarchy.

The core does not define countries, pawns, navigation, inventory, trees, voxels, production recipes, or construction methods. Domain systems provide these meanings through narrow APIs and adapters.

The principal runtime objects are **jobs, dependency edges, assignments, and resource claims**. Objectives and planning state belong to their AI controllers. Current simulation state is persisted and restored; continuous history and replay are not required.

## Design goals

1. Execute concrete work without prescribing its subject matter.
2. Allow independent AI issuers, planners, allocators, and executors to cooperate.
3. Support dependency graphs with parallel branches, shared prerequisites, and joins.
4. Allocate workers and resources, including outputs that have not yet been produced.
5. Protect committed resources from consumption by unrelated jobs.
6. Reject stale execution authority after cancellation, reassignment, or restoration.
7. Handle unavailable workers, destroyed resources, invalid targets, and failed prerequisites.
8. Restore active work from snapshots without replaying past events.
9. Keep the generic API small and domain-specific behavior outside it.

## Terms and core model

A **job** is a concrete execution contract with a stable `JobId`, a domain payload, requirements, priority, issuer authority context, lifecycle state, and structured outcome. An optional correlation identifier links it to the issuing AI's objective. A job keeps its identity across retries.

A **dependency edge** connects a prerequisite job to a dependent job. Initially, its ordering meaning is simply: the prerequisite must succeed before the dependent can run. Material flow additionally uses resource claims; an ordering edge alone does not reserve anything.

An **assignment** grants an executor authority to perform a job using selected participants. It has an `AssignmentId`, job reference, worker or executor identities, current state, and optional timing policy. Each retry or reassignment receives a new identity, which also distinguishes execution attempts. Separate attempt objects are unnecessary.

A **resource claim** has a stable identity and commits an opaque resource or quantity to a consuming job. It may refer to an existing resource or a promised output of a producer job. It records the consuming job, any producer/output reference, quantity, access mode, authorized uses, current binding, and status.

A **worker** is an entity or executor eligible to perform work. Workers need not be pawns. The first prototype uses individual pawns; the contracts should not assume that all future executors are physical workers or that every job has exactly one participant.

An **objective** is an AI-owned desired world condition, such as a completed cabin or N equipped soldiers. A **plan** is the controller's strategy for achieving that objective. Neither requires a separate generic runtime object.

The core does not require `WorkRequest`, `Plan`, `Step`, `Order`, or `Attempt` objects. Executors may have internal action sequences, and domain APIs may expose high-level requests. These do not become mandatory layers in every job.

Identifiers are opaque. Persist references and domain data rather than concrete C++ worker or world objects.

## Responsibilities

### AI controllers and planners

Controllers choose objectives, select production methods, generate jobs and dependencies, and decide whether an objective has been achieved. They own persistent planning state and recovery policy.

Automatic dependency construction uses domain-provided rules describing inputs, outputs, prerequisites, and production methods. A planner resolves unmet requirements against existing inventory, resource commitments, and planned production before creating additional work. It must avoid counting the same supply twice and detect unresolved cycles in production rules.

For example, a military AI can turn a need for swords into jobs for iron production, forging, delivery, and equipping. Maintaining food per day is an ongoing supply objective, not a prerequisite that becomes permanently satisfied when one farm is built. Its controller continues monitoring supply and issuing finite jobs.

### Job runtime

The runtime owns the accepted executable graph, validates its changes, maintains job and assignment state, and determines readiness from prerequisite outcomes and current requirements. It coordinates resource claims and publishes transient state-change events.

### Allocators and eligibility adapters

Allocators propose eligible workers and apply scheduling policy. Domain adapters validate worker eligibility, resource access, and world conditions. The runtime enforces exclusive assignment authority and rejects conflicting allocations.

### Executors and resource domains

Executors perform navigation and domain actions, retain resumable execution state, and report results. Resource domains own actual creation, movement, splitting, consumption, and destruction. Their APIs enforce the claims coordinated by the generic resource allocation system.

## Dependency graphs and planning changes

Executable prerequisite edges form a directed acyclic graph. Independent jobs may run in parallel; multiple prerequisites on one job form a join. A prerequisite may supply several consumers, subject to its output quantities and commitments.

Start with success dependencies. Planners resolve alternatives, conditional branches, cleanup, and compensation into ordinary jobs and graph edits. Mutual exclusion belongs to allocation and resource claims rather than dependency edges.

A failed, cancelled, or invalidated prerequisite blocks its dependents and notifies the responsible controller. It does not automatically fail or cancel every connected job. Dependency and correlation links do not imply ownership: cancelling one objective must not silently cancel a shared prerequisite still needed by another.

Controllers may submit additional jobs or revise active work as circumstances change. There is no expansion flag, graph-wide automatic completion, or requirement that all work be known in advance. The controller checks the actual objective state; an empty ready queue does not mean that a cabin is complete.

Graph edits must preserve acyclicity and resource commitments. Changes affecting assigned work revoke its execution authority before changed requirements take effect. Replacing a job creates a new identity and explicitly reconnects consumers and claims. Terminal jobs are not reopened. Already-applied world effects remain; controllers arrange any necessary recovery or cleanup.

Prerequisite completion establishes order, not permanent world validity. A successfully forged sword may later be destroyed. Consumers must also have valid resource bindings and pass domain checks when execution begins and when relevant world effects are applied.

## Resource allocation

Resource allocation covers items and quantities as well as worker occupancy, targets, and other exclusive or shared domain resources. The runtime coordinates opaque claims; adapters define what can conflict and enforce those claims on actual resources.

### Existing and future resources

An input can be satisfied by reserving an existing resource or by committing output of a producer job to a consumer. A future output commitment is not an available item and does not make the consumer ready by itself.

When production succeeds, actual output identities and quantities are bound to their promised consumers before unrelated jobs can acquire them. Production, claim binding, and publication must have no observable interval in which a promised item is unreserved. Shortfalls leave the affected requirements unsatisfied and trigger controller recovery. Only uncommitted output is generally available.

A producer's output cannot be promised to multiple consumers beyond its committed quantity. For example, an iron bar produced for a particular sword job remains reserved for that job while it waits, even if other forging jobs are ready.

### Claim ownership and authorized use

Claims belong to consuming jobs, not worker assignments. Losing or replacing a worker does not release the job's material commitment. A hauling job may be authorized to move an item reserved for a placement job without gaining permission to consume it for another purpose.

The resource domain must preserve the commitment across supported moves, transfers, and stack splits or merges. Any operation that consumes a reserved item must present valid authority for its intended use. Otherwise the reservation guarantee would protect scheduling but not actual consumption.

Claims can be exclusive or shared as supported by the domain. Coordinated allocation must either secure its required claims or leave no unintended partial allocation. The concrete mechanism across adapters remains an implementation choice, but intermediate state must not expose conflicting authority to synchronous callbacks.

### Consumption, release, and destruction

Successful consumption fulfills the corresponding claim. Cancellation or terminal failure releases unused claims; released carried items remain in their actual world or inventory location. Retryable execution failure preserves still-needed claims unless recovery policy explicitly changes them.

Reservations prevent unauthorized use, not physical destruction. If a bomb destroys a reserved iron bar, its binding becomes invalid, the consumer cannot proceed, and the controller arranges replacement supply or abandons the work. Destruction does not retroactively undo the producer's successful job.

Persist active claims, future commitments, and bindings as current simulation state. Revalidate them after restoration.

## Eligibility and countries

Every job carries its issuer's authority or ownership context. Country membership is a mandatory worker eligibility criterion for the initial game rule:

```text
worker.CountryId == job.CountryId
```

Country identity and semantics belong to the country domain. A narrow eligibility adapter exposes this check to allocation and execution. The generic runtime does not implement diplomacy or a universal capability language.

Check eligibility during allocation and again before execution. A country change that makes a worker ineligible revokes the assignment. Domain-specific item, tree, and site access rules must also be enforced where applicable; matching countries alone does not prove that a resource is available.

Other requirements, such as equipment, reachability, or workstation access, use domain adapters. Persist requirement data or identifiers and reconstruct adapters on load rather than serializing executable predicates. A shared capability descriptor system can be added when integrations need it, but is not a prerequisite for the prototype.

## Execution and lifecycle

A job has one lifecycle, distinct from the lifecycle of each assignment:

```text
Job:        Active -> Succeeded | Failed | Cancelled | Invalidated
Assignment: Offered -> Accepted -> Running -> Completed
            Offered -> Rejected
            Offered / Accepted / Running -> Revoked | Failed
```

Assignment transitions are validated: rejection applies before acceptance, and completion requires execution. Optional lease policies may additionally expire an assignment. An active job exposes whether it is blocked, ready, assigned, running, or suspended; these scheduling conditions do not make an unsuccessful assignment a terminal job failure.

An executor accepts or rejects an assignment, begins execution, and reports completion, interruption, or a structured failure. Retryable failure ends the assignment while the job remains active. Controller policy decides whether to retry, wait, replace work, or make the job terminal. Reassignment creates a new `AssignmentId`; no history of old assignments is required.

Terminal job states are immutable. `Failed` means the contract is abandoned as unsuccessful, `Cancelled` means authorized policy ended it, and `Invalidated` means the contract or target no longer makes sense. Results and failure reasons are machine-readable; diagnostic text is supplementary.

### Revocation and world effects

Every execution operation and result identifies its current assignment. Cancelled, revoked, superseded, or expired authority must be rejected. Before replacing an assignment, revoke the old one.

Executors validate authority immediately before world-changing operations on the simulation execution context. Checking only the final completion report is insufficient: a stale worker must not consume the log or place the voxel. Resource consumption and its corresponding world effect must complete coherently, without observable half-completion. Domain adapters define how they achieve this.

Ordinary in-process interaction uses synchronous APIs. There is no required command queue, network retry protocol, or continuous attempt log. Explicit revocation is sufficient for local executors; deadlines and renewable leases are optional policies where a concrete executor needs them.

## Progress and recovery

The prototype has no generic progress-reporting protocol or `PartiallySucceeded` terminal state. Executors retain domain state needed to continue, such as remaining chopping time or the identity of a carried log. These values are persisted when they affect future execution.

Controllers can query domain state when useful: construction can count completed blueprint voxels, inventory can locate materials, and the runtime can expose blocked or running jobs. More advanced AI may use this information to choose interruptions or change staffing without requiring a universal percentage.

Partial world effects are not rolled back merely because an assignment fails. A retry must inspect current domain state and continue or request replacement work without duplicating already-completed effects. Cleanup and compensation are ordinary controller-generated jobs.

Failures include unavailable worker, eligibility mismatch, reservation conflict, missing input, destroyed resource, invalid target, unreachable target, blocked dependency, interruption, rejection, and policy denial. The runtime exposes the facts; AI controllers choose recovery.

## First prototype: trees and wooden cabins

The AI-facing operations are `ChopTrees` and `BuildCabin`. A primitive high-level AI requests cabins at random locations. Site validation and construction planning belong to the construction controller, and an invalid site can be rejected. These high-level operations need not each map to one executable runtime job.

The [Voxel Construction Proposal](UnrealVoxelSim_Autonomous_Voxel_Construction_Proposal.md) supplies scaffold preprocessing, legal work positions, layer sequencing, and cleanup. Workers modify layer Z from intact scaffold access at Z+1. Placements within a legal layer can run in parallel; the construction controller advances after required work for that layer is complete. Scaffold creation and removal are concrete construction jobs generated by that controller.

For this example, assume one log item supplies one wooden voxel. Actual yields and conversion ratios belong to forestry and construction rules.

```text
Primitive AI: BuildCabin(blueprint, location)
    |
Construction controller: validate site and prepare scaffold/layer work
    |
    +-- ChopTree(tree T) -- promised log --> BuildWoodVoxel(site S, position P)
    |                                        collect reserved log
    |                                        haul through construction access
    |                                        consume log and place wood voxel
    |
    +-- Other currently legal placements and their material supply
    |
Advance layers, then perform scaffold cleanup and verify the cabin objective
```

The material cycle is:

1. Create a placement job with a requirement for one log and exclusive target access at P. Exclusive execution access is acquired when the placement is eligible to run; a blocked material requirement need not hold unrelated execution resources.
2. Reserve an existing log if available. Otherwise the forestry controller selects a tree, creates a chopping job, and commits its future log output to the placement. The tree target is protected against conflicting chopping work.
3. Assign an eligible worker to chop. Forestry execution produces actual log items through the relevant domain APIs.
4. Bind the promised output to the placement before exposing the log for unrelated allocation. One tree may supply several placements if it produces enough logs.
5. Once supply, prerequisite jobs, and construction access are valid, assign the placement to an eligible worker.
6. The worker collects the log, hauls it to the legal work position, and consumes it while placing the wooden voxel. The placement job succeeds.
7. Repeat for other required placements, advance layers, and complete cleanup. The construction controller verifies the resulting world state to complete its own objective.

Initially, hauling and placement are one `BuildWoodVoxel` job. Walking, collection, carrying, and placement are executor actions rather than separate core steps or orders. The same pawn may chop and build, but that is an allocator preference, not a dependency requirement.

If specialized hauling is introduced, split delivery and placement into separate jobs. Delivery receives permission to move the placement's reserved log; it does not release that reservation at the construction site.

The current [Trees and Forests design](Trees%20And%20Forests%20Proposal.md) excludes item drops. This prototype therefore also requires actual log production and item/inventory integration; job completion alone does not create resources.

## Simulation execution, persistence, and observation

Follow the existing [execution and persistence architecture](Events,%20Commands%20and%20Persistence%20proposal.md). Authoritative mutation occurs synchronously on one simulation execution context. Composition defines scheduled participant order, and actual API call order resolves competing requests. Priority is allocator policy, not an implicit same-tick batch scheduler.

Observers may react synchronously to events. Publish only after the represented mutation and its observable invariants are complete. Implementations must tolerate reentry or reject it using the existing system-wide execution-guard approach.

Snapshots save current jobs, dependency edges, active assignments, resource claims and bindings, outcomes still referenced by active work, and behavior-affecting execution state. AI objectives and planning state are saved by their owning domains. Optional deadlines use simulation time. Identifiers and entity references follow the existing persistence mapping rules.

Restore into an inactive session, resolve references, reconstruct adapters and derived scheduling state, and reconcile assignments and claims before execution resumes. Callbacks or reports from the previous session must not acquire authority in the restored session. Missing bindings leave work blocked for recovery rather than silently making it ready.

Events are transient synchronous notifications through `Events.Api`. They are not authoritative state and are not persisted. No event history, event cursors, continuous replay, or duplicate-delivery protocol is required. Observers initialize from current-state queries and subscribe without an intervening simulation mutation on the same execution context.

Queries cover current jobs and scheduling conditions, dependencies, assignments by worker, claims by resource or consuming job, and current structured blockers and outcomes. Completed records can be removed once no active work or controller requires them. Old assignments need not be retained, but their identities must not be reused in a way that authorizes stale execution.

## Module boundaries and non-goals

Use one `Jobs.Api` module as the intended production API boundary. It contains the generic contracts for jobs, dependencies, assignments, resource claims, eligibility integration, results, and events. There is no planned split into separate API modules for each concern.

Implementations and integrations use existing event and snapshot infrastructure. Domain payloads and codecs belong to their owning modules. Adding a concrete job or resource adapter should not require changing the generic runtime contracts.

`Jobs.Api` must not depend on Unreal Actors or pawns, concrete AI controllers, voxel or navigation implementations, inventory implementations, or concrete job definitions. It does not choose objectives, discover production recipes, navigate, implement construction, or define country policy.

The system does not require a universal workflow language, a global AI scheduler, one controller per worker, generic progress percentages, mandatory timed leases, automatic parent completion, or persistent execution history.

## Initial acceptance scenarios

1. Execute a direct job with an eligible worker and reject a worker from another country.
2. Execute parallel prerequisite branches followed by a dependent job; reject cyclic graph edits.
3. Build a wooden placement through chopping, reserved log production, collection, hauling, and voxel placement. Skip chopping when suitable unreserved supply already exists.
4. Make two consumers compete for limited supply; never double-commit a quantity or expose a promised output as unreserved between production and binding.
5. Preserve a consumer's material claim across worker failure, reassignment, and an authorized hauling handoff.
6. Reject stale world mutations and results after cancellation, reassignment, country changes, and session restoration.
7. Destroy a reserved resource, block its consumer, and permit the controller to arrange replacement supply without undoing the producer's completed result.
8. Block dependents after prerequisite failure without automatically cancelling unrelated work or shared prerequisites.
9. Submit additional cabin work while the objective remains active; finish it by domain-state validation rather than graph exhaustion or expansion flags.
10. Save and restore during chopping and while carrying a log, preserving action state and claims without duplicating production or consumption.
11. Release unused claims on terminal cancellation or failure and retain only outcomes needed by current work.
12. Initialize an observer from current state and deliver subsequent synchronous events without event replay or retained history.

## Remaining implementation decisions

- What concrete adapter protocol makes production, output binding, and consumption coherent across resource and world domains?
- How are quantity claims represented across item stack splits, merges, and multi-output production?
- Which initial job payloads, forestry yields, and log-to-voxel conversion rules will the prototype implement?
- What allocation policy chooses among eligible workers and competing jobs?
- When a concrete job needs several workers, how does its domain form and replace that group?
