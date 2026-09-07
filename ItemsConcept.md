# Items V1 Concept

Based on [ItemsConcept.cpp](ItemsConcept.cpp). Pseudocode omits the `UnrealVoxelSim` prefix, virtual destructors,
and constructor dependencies. `Result<T, E>` means `std::expected<T, E>`; `EntityId` means `Ecs::Api::EntityId`.
Implementation class names below are proposed.

V1 uses material items, one item slot per pawn, and multiple slots per chest. Slots count entities, not stack units.
Tree harvesting and log conversion are outside this concept.

## Ecs.Api

**Interfaces/classes:** existing entity destruction capability; no new interface required here.

**ECS components:** none added.

**Other public types**

```cpp
struct Destroying { EntityId Entity; };
```

Broadcast synchronously before any components are removed. Subscribers may inspect and mutate the entity/world.
Semantic cleanup handlers execute before structural cleanup handlers. An entity being destroyed cannot enter
destruction again or receive new contained items. Destruction completes synchronously; this state never survives a tick.

## Ecs.EnTT

Extends the existing backend's destruction path to honor `Ecs.Api::Destroying`.

**Classes**

```cpp
class Registry {
    Result<void, Ecs::Api::EntityOperationError> Destroy(EntityId entity);
};
```

Marks destruction in progress, delivers ordered cleanup notifications, then removes the entity.
Deferred destruction uses the same path when committed.

**ECS components:** none added; the reentrancy guard is transient backend state.

**Other public types:** none added.

## Containers.Api

Owns containment relationships independently of item types, world positions, and storage policies.

**Interfaces**

```cpp
interface IQuery {
    optional<EntityId> GetContainer(EntityId target) const;
    vector<EntityId> GetContents(EntityId container) const;
};
interface IInserter {
    Result<void, InsertError> InsertToContainer(EntityId target, EntityId container);
};
interface IRemover {
    Result<void, RemoveError> RemoveFromContainer(EntityId target);
};
```

Insertion transfers an already-contained entity by replacing its previous relationship.
Neither insertion nor removal changes position components or performs item-specific side effects.

**ECS components**

```cpp
struct ContainedComponent final { EntityId Container; };
```

**Other public types:** operation-specific `InsertError` and `RemoveError` enums.

## Containers

**Classes**

```cpp
class Controller : Api::IQuery, Api::IInserter, Api::IRemover {
    Controller(/* restricted ECS access, destruction notifications */);
    // Implements the API method headers above.
};
```

Maintains containment and cleans up relationships during destruction. The V1 container cleanup policy
destroys all contained entities before structural cleanup. This policy does not prescribe future drop locations.

**ECS components:** uses `Containers::Api::ContainedComponent`; no additional authoritative component required.

**Other public types:** none.

## Items.Api

Defines generic items and a shared stack representation. Stack mutation and subtype interpretation belong to domains.

**Interfaces**

```cpp
interface ICreator {
    Result<EntityId, CreateError> CreateItem(const Spatial::Api::Position& position);
};
interface IMover {
    Result<void, MoveError> MoveItem(EntityId entity, const Spatial::Api::Position& position);
};
```

Creation attaches the item marker and position, but no stack component. Movement changes position only when
`Spatial::Api::PositionComponent` is present; it does not extract contained items.

**ECS components**

```cpp
struct ItemComponent final {};
struct StackComponent { size_t Size; }; // Number of units represented; positive while the stack exists.
```

Non-stackable items have no `StackComponent`.

**Other public types:** operation-specific `CreateError` and `MoveError` enums.

## Items

**Classes**

```cpp
class Controller : Api::ICreator, Api::IMover {
    Controller(/* restricted ECS access */);
    // Implements CreateItem and MoveItem as declared above.
};
```

**ECS components:** uses `Items::Api::ItemComponent` and `Spatial::Api::PositionComponent`.
Does not manage stacks, containment, or materials.

**Other public types:** none.

## ItemStorage.Api

Owns generic item transitions between world position and containment.

**Interfaces**

```cpp
interface IInserter {
    Result<void, StoreError> Store(EntityId item, EntityId container);
};
interface IRemover {
    Result<void, ExtractError> Extract(EntityId item, Spatial::Api::Position position);
};
```

`Store` verifies `ItemComponent`, establishes/transfers containment, and removes the item's world position.
`Extract` removes containment and establishes the supplied world position. Neither enforces subtype storage policy.
Queries use `Containers::Api::IQuery` directly.

**ECS components:** none added.

**Other public types:** operation-specific `StoreError` and `ExtractError` enums.

## ItemStorage

**Classes**

```cpp
class Controller : Api::IInserter, Api::IRemover {
    Controller(/* item/position access, Containers.Api capabilities */);
    // Implements Store and Extract as declared above.
};
```

Coordinates containment and position changes as coherent operations. Failures preserve the original state;
observers do not see a partially completed transition.

**ECS components:** uses existing item, position, and containment components through the relevant capabilities.

**Other public types:** none.

## Items.Voxel.Solid.Api

Defines items representing placeable solid voxel materials and their storage rules.
`MaterialId` and `VoxelPosition` below mean the existing root voxel API types.

**Interfaces**

```cpp
interface IItemCreator {
    Result<void, CreateError> AttachMaterial(EntityId entity, MaterialId material, size_t initialStackSize);
};
interface IPlacer {
    Result<void, PlaceError> Place(EntityId item, const VoxelPosition& position);
};
interface IStackManager {
    Result<void, UsageError> Add(EntityId entity, size_t amount);
    Result<void, UsageError> Consume(EntityId entity, size_t amount);
};
interface IInserter {
    Result<void, StoreError> Store(EntityId item, EntityId container);
};
interface IStorageCreator {
    Result<void, CreateError> AttachStorage(EntityId entity, size_t itemSlots);
};
```

`AttachMaterial` attaches material and stack data to an item. The implementation defines maximum stack size per
material. `Add` and initialization enforce that limit; `Consume` rejects insufficient quantity and destroys an
exhausted item. `Place` consumes one unit iff placement succeeds, with no observable partial effect.

`Store` checks voxel-solid acceptance and capacity, then delegates to `ItemStorage::Api::IInserter`.
Storing into the current container does not require an additional slot. `AttachStorage` makes an entity eligible
to contain voxel-solid items: one slot for a pawn, N slots for a chest. Stores transfer whole item entities.

**ECS components**

```cpp
struct MaterialComponent { MaterialId Material; };
struct StorageComponent { size_t ItemSlots; }; // Entity count, not voxel-unit count.
```

**Other public types:** operation-specific `CreateError`, `PlaceError`, `UsageError`, and `StoreError` enums.

## Items.Voxel.Solid

**Classes**

```cpp
class Controller : Api::IItemCreator, Api::IPlacer, Api::IStackManager,
                   Api::IInserter, Api::IStorageCreator {
    Controller(/* item/material/stack access, containment queries, ItemStorage inserter, voxel editing */);
    // Implements the API method headers above.
};
```

Owns material stack limits, stack mutation, voxel placement, and voxel-solid storage validation.
Composition gives gameplay callers the highest-level applicable interface. This implementation receives
`ItemStorage::Api::IInserter`; `ItemStorage` receives `Containers::Api::IInserter`.
Lower layers do not duplicate subtype policy checks.

**ECS components:** uses its API's `MaterialComponent` and `StorageComponent`, plus `Items::Api::StackComponent`.

**Other public types:** none. Weapons, tools, weight, conversion recipes, and alternative storage policies are deferred.
