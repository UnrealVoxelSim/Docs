// UnrealVoxelSim namespace prefix omitted for simplicity

namespace Production::Api
{
	class IWoodProduction
	{
		virtual std::expected<std::vector<EntityId>, ProductionError> ProduceWoodenPlanks(EntityId logItem, size_t logsToConsume, const Spatial::Api::Position& outputPosition);
		
		virtual std::expected<std::vector<EntityId>, ProductionError> ProduceWoodenScaffolds(EntityId logItem, size_t logsToConsume, const Spatial::Api::Position& outputPosition);
	}
}