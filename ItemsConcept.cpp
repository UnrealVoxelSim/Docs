// UnrealVoxelSim namespace prefix omitted for simplicity

// Extensions for ECS framework
namespace Ecs::Api
{
	// Broadcast synchronously before any components are removed.
	// Subscribers may inspect and mutate the entity/world.
	// Semantic cleanup handlers execute before structural cleanup handlers.
	// entity already being destroyed cannot enter destruction again
	struct Destroying
	{
		EntityId Entity;
	}

}

namespace Containers::Api
{
	class IQuery
	{
	public:
		virtual std::optional<Ecs::Api::EntityId> GetContainer(Ecs::Api::EntityId target) const = 0;
		
		virtual std::vector<Ecs::Api::EntityId> GetContents(Ecs::Api::EntityId container) const = 0;
	};

	class IInserter
	{
		// Implementation does not remove PositionComponent or anything else. It only establishes Containment relationship.
		// containment side-effects are handled by separate cross-domain interactors like ItemStorage.Api
		// If entity is already contained, it is transfered to the new container, removing old relationship.
		virtual std::expected<void, InsertError> InsertToContainer(
			Ecs::Api::EntityId target
			Ecs::Api::EntityId container,
		) = 0;
	};

	class IRemover
	{
		// Implementation does not reattach PositionComponent or anything else. It only removes Containment relationship.
		// de-containment side-effects are handled by separate cross-domain interactors like ItemStorage.Api
		virtual std::expected<void, RemoveError> RemoveFromContainer(
			Ecs::Api::EntityId target
		) = 0;
	};

	// Attached to entity to establish containment relationship
	struct ContainedComponent final
	{
		Ecs::Api::EntityId Container;
	};

}


namespace Items::Api
{
	// Abstracts consumer for item-producing interfaces.
	// Concrete implementations may attach PositionComponents and "drop" items inside Voxel World
	// Others may immediately insert item into a container, skipping attachment-detachment of PositionComponent.
	// Implementations should be domain-specific. For example, Items::Voxel::Solid should provide its own receivers for voxel-solid items.
	class IReceiver
	{
	public:
		virtual ~IReceiver() = default;

		// Success: establishes the destination, replacing the previous location.
		// Failure: leaves the item and its original location unchanged.
		// V1 receivers preserve entity identity and stack contents.
		virtual std::expected<void, ReceiveError> Receive(
			Ecs::Api::EntityId item) = 0;
	}; 
	
	class IPositionReceiverFactory
	{
	public:
		virtual std::unique_ptr<IReceiver> Create(const Spatial::Api::Position& position) = 0;
	}
	
	class ICreator
	{
		// Creates a dummy item entity without a position component. Does not attach StackComponent by itself.
		virtual std::expected<EntityId, CreateError> CreateItem();
	}
	
	/* Probably not needed
	class IMover
	{
		// Changes item position, if PositionComponent present.
		virtual std::expected<void, MoveError> MoveItem(EntityId entity, const Position& position);
	}
	*/

	
	// Items can be both stackable and non-stackable. Non-stackable items don't have this component.
	// Items::Api is not responsible for managing stacks. This is domain-defined behavior.
	// StackComponent is only a shared representation for stacks. Sub-domains define mutation and interpretation.
	struct StackComponent
	{
		// Number of units this items represents.
		std::size_t Size;
	}
	
	// Intentionally empty, it only tells that an entity is an item.
	// Payload may be added later.
	struct ItemComponent final
	{
	};
}

namespace ItemStorage::Api
{

	class IInserter
	{
	public:
		// Verifies that item has Items::Api::ItemComponent and performs the
		// generic item world <-> containment transition.
		// Does not enforce subtype-specific storage acceptance/capacity rules.
		virtual std::expected<void, StoreError> Store(
			Ecs::Api::EntityId item,
			Ecs::Api::EntityId container) = 0;
	};

	class IRemover
	{
	public:
		virtual std::expected<void, ExtractError> Extract(
			Ecs::Api::EntityId item,
			IReceiver& receiver) = 0;
	};
	
	class IStorageReceiverFactory
	{
	public:
		virtual std::unique_ptr<Items::Api::IReceiver> Create(Ecs::Api::EntityId container) = 0;
	}

	// No need for query, Containers::Api::IQuery can be used directly.

}


// This module is intentionally focused only on items that represent placeable voxels
// The game will have other items like weapons and tools in the future, but their semantics do not have to be shared with
// voxel-items.
namespace Items::Voxel::Solid::Api
{
	class ICreator
    {
    public:
        virtual ~ICreator() = default;
		
		// Uses Items::Api::ICreator internally, attaches MaterialComponent and invokes IReceiver
        virtual std::expected<EntityId, CreateError> Create(
            MaterialId material,
            std::size_t quantity,
            Items::Api::IReceiver& receiver) = 0;
    };
	
	struct MaterialComponent
	{
		Voxel::Solid::Api::MaterialId Material;
	}
	
	class IPlacer
	{
	public:
		// Uses one unit from the voxel-solid item to place a voxel of its
		// MaterialComponent::Material at position.
		//
		// The item is consumed iff voxel placement succeeds.
		// May destroy the item entity if the last stack unit is consumed.
		virtual std::expected<void, PlaceError> Place(
			Ecs::Api::EntityId item,
			const Voxel::Api::Position& position) = 0;
	};
	
	class IStackManager
	{
		virtual std::expected<void, UsageError> Add(EntityId entity, size_t amount);
		
		// May destroy the entity
		virtual std::expected<void, UsageError> Consume(EntityId entity, size_t amount);
	}
	
	class IInserter
	{
	public:
		// Enforces voxel-solid-specific storage policy and delegates the
		// generic storage transition to ItemStorage::Api::IInserter.
		virtual std::expected<void, StoreError> Store(
			Ecs::Api::EntityId item,
			Ecs::Api::EntityId container) = 0;
	};
		
	class IStorageCreator
	{
		// Attaches StorageComponent to an entity, making it a valid container for voxel-solid items.
		virtual std::expected<void, CreateError> AttachStorage(EntityId entity, size_t itemSlots)
	}
	
	// Voxel::Solid-specific storage component, telling that this entity can hold voxel-solid items
	struct StorageComponent
	{
		// Number of entities, not total number of placeable voxels.
		size_t ItemSlots;
	}
}