# Proposal: Simplified Autonomous Voxel Construction

## Goal

Implement a construction system for autonomous pawns that can execute imported voxel building blueprints at runtime without requiring a generic construction-planning or scaffold-planning solver.

The system should support:

- autonomous workers;
- limited pawn carrying capacity;
- repeated trips between material sources and construction sites;
- directional normal-world navigation;
- tall buildings and towers;
- enclosed rooms and arbitrary internal voids;
- underground buildings and mines;
- voxel-by-voxel visible construction;
- offline blueprint preprocessing;
- deterministic validation and execution.

The main simplification is to avoid asking pawns to discover how to make a structure reachable while they are building it.

Instead, construction creates a temporary, highly traversable scaffold medium around or inside the future building. Pawns can move through scaffold voxels in any direction. The actual building is then produced by progressively replacing scaffold voxels with permanent voxels or removing scaffold where the final blueprint requires empty space.

---

## Core Idea

Introduce a special temporary voxel type:

```text
ScaffoldVoxel
```

A scaffold voxel is part of the voxel world, but has special traversal rules.

While inside connected scaffold voxels, construction workers can move slowly in all directions, approximately as if they were swimming through a 3D lattice.

Normal terrain traversal remains unchanged. For example, a pawn may still be able to jump down a cliff without being able to climb back up. Scaffold traversal is intentionally different and reversible.

Conceptually:

```text
normal world:
    directed and physically constrained movement

scaffold volume:
    slow, bidirectional 3D movement
```

The scaffold therefore provides temporary construction access without requiring the AI to plan ladders, stairs, ramps, or arbitrary scaffold structures.

Visually, scaffold voxels do not need to look like solid cubes. They can be rendered as a sparse wooden or metal framework so that pawns appear to climb through construction scaffolding.

---

## Blueprint Preprocessing

Imported blueprints should be processed before they can be used by runtime pawns.

A blueprint describes the desired final state of a bounded voxel region.

Each voxel in the final blueprint is one of:

```text
solid target material
empty space
```

Preprocessing should convert this final-state representation into a construction plan.

The compiler does not need to solve arbitrary pawn navigation through intermediate building states.

Instead, it generates temporary scaffold space that guarantees construction access.

The result is approximately:

```text
Blueprint
    |
    v
Validate final structure
    |
    v
Generate temporary construction space
    |
    v
Divide construction into layers or stages
    |
    v
Generate scaffold creation jobs
    |
    v
Generate voxel replacement/removal jobs
    |
    v
Generate scaffold cleanup jobs
    |
    v
Construction Plan
```

The exact runtime job representation is deliberately unspecified.

---

## Surface Construction

For an above-ground structure, generate scaffold space around the construction bounds.

The simplest version is:

```text
building bounding box
+
small horizontal clearance
+
vertical clearance above the highest layer
```

Initially, workers create the scaffold volume.

Once enough scaffold exists, they construct the actual building layer by layer.

A useful invariant is:

> Workers modify a target layer while navigating through an intact scaffold layer above it.

Example:

```text
Layer Z+1:
SSSSSSSSSSSS
S    P     S
SSSSSSSSSSSS

Layer Z:
S??##???#??S

Layer Z-1:
S##########S
```

`S` = scaffold  
`P` = pawn  
`?` = scaffold waiting to be converted  
`#` = completed building voxel

A worker on layer `Z+1` modifies the voxel directly below it on layer `Z`.

The target scaffold voxel is replaced with the desired permanent material:

```text
scaffold -> stone
scaffold -> wood
scaffold -> metal
...
```

If the final blueprint requires that voxel to be empty:

```text
scaffold -> air
```

This means enclosed rooms are naturally carved out during construction rather than requiring workers to enter them before walls are sealed.

---

## Layer-by-Layer Construction

Construction progresses in a fixed vertical direction, similar to a 3D printer.

Conceptually:

```text
for each target layer Z from bottom to top:

    guarantee intact scaffold access at Z + 1

    allow workers to modify target voxels at Z

    wait until required work for Z is complete

    advance to the next layer
```

Workers do not need to follow a strict voxel sequence inside a layer.

As long as they operate from an untouched scaffold work layer above, multiple workers may choose jobs independently.

For example:

```text
worker A -> nearest stone voxel
worker B -> nearest wood voxel
worker C -> empty voxel that must be cleared
```

The important synchronization point is between construction layers, not necessarily between individual voxel placements.

---

## Why This Solves Reachability

A worker does not navigate through the partially completed building.

Instead, the worker navigates through the temporary scaffold medium.

A typical hauling cycle becomes:

```text
material source
    |
    v
construction entrance
    |
    v
scaffold network
    |
    v
current work layer
    |
    v
place carried materials
    |
    v
return through scaffold
    |
    v
material source
```

This works naturally with limited carrying capacity.

A pawn may carry only a few blocks at a time and make many trips. The construction planner does not need to plan those trips individually.

The only important guarantee is that the current scaffold network remains connected to the construction entrance.

Because scaffold movement is bidirectional, the system avoids the normal-world problem where a pawn can jump somewhere but cannot return.

---

## Avoiding a Full Bounding-Box Scaffold Volume

Filling the entire construction volume with scaffold is the easiest approach conceptually, but it may create excessive work.

A more efficient variation uses:

- an exterior scaffold access shell or vertical access route;
- one active horizontal scaffold work layer;
- optionally one next work layer prepared in advance.

Conceptually:

```text
        next scaffold deck
     SSSSSSSSSSSSSSSSS

        active work deck
     SSSSSSSSSSSSSSSSS
     S               S

        target layer
     S ###..#####..# S

        completed layers
     S ############# S
     S ############# S
     S               S
```

Before converting the current work deck into the next building layer, create the next scaffold deck above it and move workers there.

This produces a rolling construction platform rather than a scaffold-filled cuboid.

Either implementation is valid. The full-volume version is simpler; the rolling-deck version reduces temporary voxel count and scaffold-building labor.

---

## Scaffold Cleanup

After the final building layer is complete, temporary scaffold must be removed.

Because the scaffold shape is generated by the construction system rather than discovered dynamically, cleanup should also follow a deterministic pattern.

For example:

```text
remove top work deck
    |
    v
remove exterior scaffold from top downward
    |
    v
finish at construction entrance
```

Workers should normally remove scaffold while standing in scaffold that will be removed later.

For a planar work deck, a deterministic sweep or snake pattern can guarantee that the worker always retains a route to the remaining scaffold network.

Example:

```text
>>>>>>>>v
v<<<<<<<<
>>>>>>>>v
v<<<<<<<<
>>>>>>>>X
```

`X` is the final connection to the scaffold exit.

Generic path planning for scaffold dismantling should not be necessary.

---

# Underground Construction

Underground structures use the same overall model, but the temporary construction space should not normally be a large rectangular clearance box.

Excavating a full bounding-box shell around an underground structure could leave an unrealistic hidden cavern and would make restoration unnecessarily difficult.

Instead, underground preprocessing should classify construction space into three categories:

```text
1. final solid voxels
2. final empty voxels
3. temporary excavation
```

Final empty voxels should be used as construction access whenever possible.

Temporary excavation should be generated only when additional workspace is required.

---

## Use Final Empty Space as Access

Mines, tunnels, rooms, shafts, and corridors naturally contain empty volume in their final state.

That space can initially contain scaffold.

For example, the final mine tunnel might be:

```text
████████████████
██............██
████████████████
```

During construction:

```text
████████████████
██SSSSSSSSSSSS██
████████████████
```

Workers use this scaffold-filled tunnel to move, haul materials, install supports, line walls, place floors, and perform other work.

At the end:

```text
scaffold -> air
```

No backfilling is required because those voxels are supposed to remain empty.

This should be the preferred form of underground construction access.

---

## Temporary Underground Excavation

Some underground blueprints may require workspace outside their final empty volume.

For those cases, generate narrow temporary service tunnels rather than a large clearance box.

A useful restriction is:

> Temporary excavation outside the final blueprint should form a rooted tree connected to the construction entrance.

Example:

```text
Entrance
   |
   S
   |
   S------S------S
   |
   S
   |
 [work area]
```

Every newly excavated scaffold voxel must connect to an already reachable scaffold voxel.

This guarantees that creation is simple:

```text
reachable scaffold
    |
    v
excavate adjacent rock
    |
    v
replace with scaffold
```

No generic scaffold-layout solver is needed if the temporary network is generated using simple rules.

---

## Underground Cleanup by Tree Retraction

A rooted temporary scaffold tree can be removed in reverse order.

Always backfill leaves before their parents.

Example:

```text
Entrance
   |
   A
   |
   B------C------D
   |
   E
```

Remove `D` first, then `C`, and so on.

The worker fills a scaffold voxel while standing in its parent.

Conceptually:

```text
for each temporary scaffold node in post-order:

    worker stands in parent node
    scaffold node -> backfill
```

The key invariant is:

> A temporary excavation voxel may be backfilled only when no unfinished temporary scaffold depends on it for access.

This guarantees that cleanup cannot trap workers.

---

## Backfill Instead of Perfect Terrain Restoration

Temporary underground excavation does not need to restore the exact original geology.

Instead, use a generic backfill material such as:

```text
packed rock
rubble
construction fill
disturbed earth
```

Thus:

```text
natural rock
    ->
temporary scaffold
    ->
backfill
```

This is easier to simulate and arguably more believable than reconstructing untouched geology.

Backfill may optionally have different properties from natural rock, such as:

- lower structural strength;
- faster excavation;
- different visual appearance;
- reduced resource yield.

This can preserve evidence of old construction activity in the world.

---

## Optional Underground Service Shaft

For large underground structures, preprocessing may generate one standard construction shaft connecting the building to:

- the surface;
- an existing tunnel;
- a cave;
- another reachable underground region.

The shaft acts as the root of the scaffold network.

After construction it can either:

```text
remain as permanent access
```

or:

```text
be backfilled from its deepest point toward the entrance
```

or:

```text
be incorporated into the final building as stairs, elevator space, or a mine shaft
```

This provides a predictable logistics route without requiring the planner to discover arbitrary underground access.

---

# Construction Rules and Invariants

The construction system should favor simple invariants over general planning.

Recommended invariants:

### Scaffold traversal is reversible

Inside connected scaffold voxels, workers can travel in all directions.

This separates construction movement from ordinary directed movement.

### Active workers always remain connected to the construction entrance

Workers should never rely on newly completed permanent voxels as their only escape route.

### Workers normally modify voxels from scaffold space

Permanent geometry should not be required to remain navigable during construction.

### Layer completion is synchronized

A new layer is not allowed to invalidate the scaffold work layer currently used by workers.

### Temporary underground excavation has a simple topology

Prefer rooted trees or similarly deterministic structures that can be dismantled in exact reverse order.

### Final empty blueprint space is preferred over temporary excavation

Rooms, corridors, shafts, and tunnels are useful construction access and should be reused whenever possible.

### Temporary excavation is restored with backfill

Do not require exact reconstruction of the original terrain.

---

# Runtime Worker Behavior

Runtime worker intelligence should remain simple.

A worker does not need to understand the entire building plan.

The construction system exposes currently valid jobs.

Typical jobs include:

```text
deliver material
place target voxel
remove scaffold voxel
excavate rock
place scaffold
backfill temporary excavation
```

A worker repeatedly performs local tasks:

```text
choose available job
    |
    v
collect required material if needed
    |
    v
navigate to scaffold-accessible work position
    |
    v
perform job
    |
    v
return or choose another job
```

The preprocessing system is responsible for ensuring that these jobs are globally safe to execute.

This preserves the desired autonomous-settlement feeling: many pawns can independently participate in construction without themselves being construction planners.

---

# Blueprint Validation

The blueprint editor should validate final structures before they are made available to the simulation.

Validation can remain conservative.

It does not need to prove that every theoretically possible voxel structure can be built.

It only needs to decide whether the structure can be built under the game's construction rules.

Possible validation checks include:

```text
building dimensions are within supported limits
required foundation exists
final structure satisfies structural rules
surface clearance can be generated
underground access can be generated
temporary excavation does not exceed configured limits
required construction entrance or service shaft can be created
```

If preprocessing cannot produce a valid scaffold-based construction plan, the blueprint should be rejected with a useful explanation.

---

# Intended Result

The main benefit of this approach is that difficult construction planning is replaced by controlled temporary topology.

Instead of solving:

```text
Given arbitrary voxel geometry and directional pawn movement,
discover a safe sequence of construction actions.
```

the game solves:

```text
Generate a predictable scaffold workspace,
then convert that workspace into the target voxel structure
using a fixed sequence of construction stages.
```

This supports visually detailed voxel-by-voxel construction while keeping worker AI relatively simple.

The player can observe autonomous pawns:

- hauling limited quantities of resources;
- repeatedly travelling between stockpiles and the site;
- climbing through visible scaffolding;
- constructing buildings one layer at a time;
- excavating underground rooms;
- installing walls and supports;
- dismantling or backfilling temporary construction access.

The complexity is therefore concentrated in deterministic blueprint preprocessing rather than in a generic runtime construction solver.
# Building Repair

Voxel structures are expected to suffer frequent local damage. Re-running the full construction process for every missing voxel would be visually strange and unnecessarily expensive.

Repairs should therefore use a separate, tiered strategy.

The general rule is:

```text
try the smallest repair operation first
    |
    v
escalate only when necessary
```

The repair system does not need a generic construction solver. It can reuse the same temporary-scaffold principle at a much smaller scale.

---

## Repair Tiers

Recommended repair escalation:

```text
damaged voxel or region
    |
    v
directly repairable?
    |
   yes
    |
    v
direct patch

otherwise
    |
    v
small inaccessible damage?
    |
   yes
    |
    v
temporary repair access

otherwise
    |
    v
localized reconstruction practical?
    |
   yes
    |
    v
rebuild local region

otherwise
    |
    v
full building reconstruction
```

This allows a single missing voxel to be repaired naturally while still providing a simple fallback for severe destruction.

---

## Direct Repair

The simplest case is damage that can be reached using normal pawn navigation.

For every missing or incorrect target voxel, first test whether a pawn can reach a valid work position and perform the repair directly.

Example:

```text
########.#######
        P
```

The repair job is simply:

```text
collect required material
    |
    v
navigate to repair position
    |
    v
replace missing voxel
```

No scaffold should be created if ordinary repair is sufficient.

This should handle many common cases such as:

- missing exterior wall voxels;
- damaged floors;
- damage near doors or walkable surfaces;
- low parts of structures;
- damage reachable from permanent stairs or internal walkways.

---

## Temporary Repair Access

If damage is local but cannot be reached directly, generate a small temporary scaffold access route.

The access route should be deliberately simple.

Examples include:

```text
vertical scaffold mast
horizontal scaffold bridge
straight repair corridor
L-shaped repair corridor
small scaffold shaft
```

These are known construction primitives, not arbitrary scaffold layouts.

The goal is to create a temporary reversible path between an accessible region and the damaged area.

---

## Exterior Repair with a Scaffold Mast

Damage high on a tower can be repaired with a narrow vertical scaffold mast.

Example:

```text
       S##.##
       S#####
       S#####
       S#####
_______S#####
```

`S` = temporary scaffold

Workers can:

```text
enter scaffold
    |
    v
climb to damaged level
    |
    v
repair missing voxel
    |
    v
descend
    |
    v
dismantle scaffold top-down
```

This avoids surrounding the entire structure with scaffolding.

---

## Internal Repair Corridors

An inaccessible interior voxel can be repaired by temporarily opening a narrow path from an accessible side.

Example:

```text
###############
#             #
#       XSSSSS
#             #
###############
```

`X` = damaged voxel  
`S` = temporary repair access

The access corridor may pass through:

- empty space;
- scaffold;
- temporarily removed building voxels.

If an intact building voxel must be removed, remember its intended material and restore it after the repair.

Conceptually:

```text
outside
    |
    v
open temporary corridor
    |
    v
repair target damage
    |
    v
restore corridor from deepest point outward
```

---

## Reverse Cleanup

Temporary repair access should be removed in reverse order.

For an access chain:

```text
Entrance -> A -> B -> C -> Damage
```

cleanup is:

```text
repair Damage
restore C while standing at B
restore B while standing at A
restore A while standing outside
```

The important invariant is:

> Never restore or remove a temporary access voxel while unfinished repair work still depends on it.

This guarantees that workers retain an escape route.

The same principle used for underground scaffold-tree cleanup therefore applies to repairs.

---

## Simple Repair-Access Generation

Repair access does not need to be globally optimal.

A conservative deterministic strategy is sufficient.

For internal damage, one simple option is:

```text
find nearest accessible exterior face
    |
    v
create a straight or axis-aligned corridor
    |
    v
repair damage
    |
    v
close corridor in reverse order
```

If later needed, this can be improved with a simple weighted voxel path search.

Possible traversal costs:

```text
empty voxel           low cost
existing scaffold     low cost
cheap wall            moderate cost
expensive wall        high cost
critical structure    forbidden
```

This is still much simpler than generic construction planning because it finds only one temporary access route through an otherwise static structure.

---

## Damage Clustering

Nearby damaged voxels should normally be grouped into repair regions.

For example:

```text
###############
###...#########
###....########
####..#########
###############
```

should be treated as one damaged region rather than a collection of independent single-voxel repairs.

Possible clustering rules include:

```text
face-connected damage
small Manhattan-distance threshold
expanded damaged bounding boxes
```

The exact grouping rule can remain simple.

The goal is to avoid generating multiple scaffold routes for damage that can reasonably be repaired together.

---

## Local Reconstruction

If a damage cluster is too large for direct patching but still affects only a limited portion of the building, rebuild only that local region.

Example:

```text
Before:

############
############
############
############

After damage:

######......
######......
######......
############
```

Determine a local reconstruction region:

```text
damaged bounding box
+
small clearance margin
```

Then apply the normal scaffold-based construction approach only inside that region.

Conceptually:

```text
identify damaged region
    |
    v
expand region slightly
    |
    v
temporarily scaffold local volume
    |
    v
reconstruct target voxels
    |
    v
remove local scaffold
```

Some intact voxels may be deliberately dismantled and rebuilt if that makes the repair operation simpler.

The system should not try to mathematically minimize the number of intact voxels disturbed.

A simple and predictable local reconstruction is preferable to a complicated minimal-change planner.

---

## Full Reconstruction

Full building reconstruction remains the final fallback.

Use it when:

```text
damage is widespread
local repair regions overlap most of the structure
required repair access becomes excessively large
structural collapse invalidates large portions of the blueprint
```

Conceptually:

```text
clear affected building volume
    |
    v
restart normal construction plan
```

Full rebuilding should therefore be rare for small damage but available for catastrophic destruction.

---

## Suggested Repair Classification

Exact thresholds should be determined through gameplay testing, but a simple initial policy may look like:

```text
small number of exposed damaged voxels:
    direct repair

small inaccessible cluster:
    temporary scaffold mast/corridor

moderate local destruction:
    local reconstruction

large percentage of building destroyed:
    full reconstruction
```

The important part is the escalation strategy, not the exact numeric thresholds.

---

## Repairs and Limited Carry Capacity

Repair jobs should preserve the same logistics behavior as normal construction.

Workers still need to:

```text
collect materials
    |
    v
travel to repair access
    |
    v
perform as much work as carried materials allow
    |
    v
return for more materials
```

Temporary scaffold access therefore needs to remain reversible until the associated repair region is complete.

Cleanup begins only after:

```text
all required permanent repairs are finished
and
all workers have exited the temporary repair region
```

---

## Optional Visible Repair History

Repaired sections may optionally retain visible evidence of previous damage.

Possible representations include:

```text
slightly different texture variation
repair age
repair quality
disturbed masonry
backfill around underground repairs
```

This is optional and should not complicate the core repair algorithm.

It may, however, make repeatedly damaged settlements visually more interesting.

---

## Repair Design Principle

The repair system should follow the same general philosophy as initial construction:

> Create a deliberately simple temporary topology, perform the required work inside it, then retract that temporary topology in reverse.

For initial construction, the temporary topology is a scaffold work volume or moving scaffold deck.

For underground construction, it is final empty space plus a simple temporary service network.

For repair, it is usually a small scaffold mast, corridor, shaft, or local reconstruction volume.

This avoids introducing a generic repair-planning solver while allowing buildings to recover naturally from both minor and severe voxel damage.
