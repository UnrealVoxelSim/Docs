// UnrealVoxelSim namespace prefix omitted for simplicity

namespace Production::Api
{
	// Success:
	//   Consumes exactly logsToConsume logs.
	//   Delivers all output items and returns their identities.
	//
	// Failure:
	//   Preserves the input stack and its location.
	//   Leaves no output items or destination changes.
	class IWoodProduction
	{
		virtual std::expected<std::vector<EntityId>, ProductionError> ProduceWoodenPlanks(EntityId logItem, size_t logsToConsume, IReceiver& receiver);
		
		virtual std::expected<std::vector<EntityId>, ProductionError> ProduceWoodenScaffolds(EntityId logItem, size_t logsToConsume, IReceiver& receiver);
	}
}