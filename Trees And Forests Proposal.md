# Trees and Forests V1 Design

## Scope

V1 introduces deterministic voxel trees and minimal forest generation. Growth, seasons, falling-voxel simulation, and
item drops are excluded. Delivery is split into runtime Trees followed by Forest Generation built on the Trees API.

## Authority and Composition

```text
Tree entity
├── Trees::Api::TreeComponent
├── Voxel::Api::BoundsComponent
└── Voxel::Api::MembershipComponent

Voxel world
└── authoritative material and occupancy
```

The tree entity owns tree identity, lifecycle, and entity-to-voxel association. Voxels do not contain entity IDs.
Membership is exclusive, and external voxel placements never join a tree.

## Generic Voxel Components

`BoundsComponent` contains a half-open `Region`; its minimum is inclusive and maximum exclusive. Bounds contain every
member but may remain conservative after removal.

`Voxel::Api::Set` stores unique world-space `Position` values in deterministic sorted order. Its initial contiguous
representation makes memory proportional to member count and provides logarithmic membership tests. A future adaptive
or chunked encoding can replace this representation without changing tree semantics.

```cpp
struct BoundsComponent final
{
    Region Bounds{};
};

struct MembershipComponent final
{
    Set Members;
};
```

Because membership uses world positions, bounds are not needed to interpret the set. The invariant is that bounds
contain all members.

## Solid Change Events

One event is published after each successful atomic solid-edit batch:

```cpp
struct CellChange final
{
    Voxel::Api::Position Position{};
    Cell Previous{};
    Cell Current{};
};

struct Changed final
{
    std::vector<CellChange> Cells;
    std::vector<Voxel::Api::Region> Regions;
};
```

`Cells` is authoritative and sorted in deterministic Z/Y/X order. `Regions` is derived by coalescing contiguous X runs
and remains available to broad invalidation consumers such as Navigation. Events are moved into immediate synchronous
publication; listeners receive them by const reference. Failed edits publish nothing.

Trees queries the spatial index with the regions, deduplicates candidates, and applies the complete cell batch before
deciding what behavior it triggers.

## Spatial Index

`Spatial.Index.Voxel.Api` has separate templated read and write contracts. Insertion returns an opaque `EntryId` used for
updates and removal, avoiding payload scans. Queries accept batches of half-open regions.

The initial `Spatial.Index.Voxel.Chunked` implementation maps fixed-size voxel chunks to entry handles, deduplicates
candidates, and performs exact region intersection. Trees sorts entity candidates before behavior, so unordered bucket
storage cannot influence simulation order.

The index is shared infrastructure for voxel-backed entities and is derived state. Handles are never persisted. Trees
rebuilds entries from ECS bounds and membership during construction/restoration.

## Trees API and Creation

`Trees.Api` defines `SpeciesId`, `StandardSpecies::Oak`, `ShapeSeed`, `Species`, `TreeComponent`, `IPlanter`, and
`IRemover`.

```cpp
struct CreateRequest final
{
    Voxel::Api::Position Root{};
    SpeciesId Species{};
    ShapeSeed Seed{};
};
```

Trees owns deterministic shape generators. Callers never submit raw voxel lists. V1 supplies one oak generator using
`StandardMaterials::Trunk` and `StandardMaterials::Leaves`.

Planting stages an ECS entity, atomically attempts all generated voxel placements, then registers bounds and changes the
entity to `Standing`. Occupied or out-of-world volume rejects the entire tree and destroys the staged entity. Public
tree removal removes both all remaining member voxels and the entity.

## Lifecycle and Chopping

```cpp
enum class State
{
    Planting,
    Standing,
    PendingStabilityCheck,
    PendingRemoval,
};
```

`Planting` is transient within creation. The other states are authoritative.

For a relevant change batch, Trees first removes every position that is no longer a tree material from membership.
Removing only leaves has no further effect. Removing trunk marks a standing tree `PendingStabilityCheck`; stability is
evaluated during the next Trees simulation step.

Stability uses six-face connectivity:

1. trunk connected to the root through trunk is supported;
2. leaves can be reached outward through leaves from supported trunk;
3. leaves never support trunk;
4. unsupported members collapse;
5. when the root is absent, the entire remainder collapses.

Foliage may remain when it is still connected through foliage to supported canopy. This accepted V1 behavior avoids a
persisted generator-specific parent graph.

Before voxel removal, Trees removes collapsing positions from membership. Nested synchronous change events therefore
cannot route those positions back into the same collapse. The removal is one atomic batch within one Trees step and the
voxels simply disappear in V1.

Future voxel physics receives detached positions only after Trees removes them from membership. Trees owns semantic
attachment; physics acts only on unclaimed voxels, preventing competing support policies.

## Persistence Model

Persistence adapters are deferred, but the logical model is fixed. Persist species, root, shape seed, lifecycle, current
membership, and either bounds or enough data to recompute them. Do not persist index handles, subscriptions, stability
scratch storage, or the V1 computed collapse set.

If a trunk is removed and a checkpoint occurs before the next Trees step, `PendingStabilityCheck` and updated membership
are saved. Restoration rebuilds the index, and the next step deterministically recomputes and executes collapse.

V1 collapse is simulation-step atomic, so no meaningful work buffer exists at a checkpoint. If collapse is later
budgeted across ticks, remaining work becomes durable domain state and must be persisted.

## Forest Generation

`Forest.Generation.Api` accepts a search region, candidate count, species, and deterministic seed. Its implementation
samples candidate columns, finds an injected allowed ground material, derives a tree shape seed, and delegates planting
to `Trees::Api::IPlanter`. Occupied or geometrically invalid candidates are skipped and candidate/planted counts are
returned. Forest generation never paints tree voxels directly.

Biomes, climate, moisture, density fields, and species distributions remain future inputs.

## V1 Invariants

1. Voxel state is authoritative for occupancy and material.
2. Membership is authoritative for entity-to-voxel association.
3. Every tree has tree, bounds, and membership components.
4. Bounds contain every member position.
5. External placement never adds membership.
6. Structural membership is exclusive.
7. Successful public removal leaves neither entity nor member voxels.
8. Complete change batches are applied before stability decisions.
9. Leaves do not support trunk.
10. Pending stability is durable; computed per-step work is not.
11. Spatial indexing is reconstructible derived state.
12. Generic voxel and spatial modules do not depend on Trees.
