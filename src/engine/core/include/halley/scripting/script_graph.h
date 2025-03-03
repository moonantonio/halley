#pragma once
#include "script_state.h"
#include "halley/entity/entity_id.h"
#include "halley/bytes/config_node_serializer.h"
#include "halley/graph/base_graph.h"

namespace Halley {
	class GraphNodeTypeCollection;
	class IScriptNodeType;
	class ScriptNodeTypeCollection;
	class ScriptGraph;
	class World;

	class ScriptGraphNode final : public BaseGraphNode {
	public:
		ScriptGraphNode();
		ScriptGraphNode(String type, Vector2f position);
		ScriptGraphNode(const ConfigNode& node);
		
		void serialize(Serializer& s) const override;
		void deserialize(Deserializer& s) override;

		void feedToHashEx(Hash::Hasher& hasher, bool assetOnly) const;
		bool canDraw() const override { return !parentNode; }

		void assignType(const GraphNodeTypeCollection& nodeTypeCollection) const override;
		void clearType() const override;
		const IGraphNodeType& getGraphNodeType() const override;
		const IScriptNodeType& getNodeType() const;

		OptionalLite<GraphNodeId> getParentNode() const { return parentNode; }
		void setParentNode(OptionalLite<GraphNodeId> id) { parentNode = id; }

		void offsetNodes(GraphNodeId offset) override;

		std::unique_ptr<BaseGraphNode> clone() const override;

	private:
		mutable const IScriptNodeType* nodeType = nullptr;
		OptionalLite<GraphNodeId> parentNode;
	};

	struct ScriptGraphNodeRoots {
		struct Entry {
			Range<GraphNodeId> range;
			GraphNodeId root;

			Entry() = default;
			Entry(Range<GraphNodeId> range, GraphNodeId root);
			Entry(const ConfigNode& node);
			ConfigNode toConfigNode() const;
		};

		Vector<Entry> mapping;

		ScriptGraphNodeRoots() = default;
		ScriptGraphNodeRoots(const ConfigNode& node);
		ConfigNode toConfigNode() const;

		void addRoot(GraphNodeId id, GraphNodeId root);
		GraphNodeId getRoot(GraphNodeId id) const;
		void clear();
	};
	
	class ScriptGraph final : public BaseGraphImpl<ScriptGraphNode>, public std::enable_shared_from_this<ScriptGraph> {
	public:
		struct FunctionParameters {
			uint8_t nOutput = 1;
			uint8_t nDataInput = 0;
			uint8_t nTargetInput = 0;
			uint8_t nDataOutput = 0;
			uint8_t nTargetOutput = 0;
			Vector<String> inputNames;
			Vector<String> outputNames;
			String icon;
		};

		ScriptGraph();
		ScriptGraph(const ConfigNode& node);

		void load(const ConfigNode& node);
		void load(const ConfigNode& node, Resources& resources) override;
		void parseYAML(gsl::span<const gsl::byte> data);

		bool isPersistent() const;
		bool isMultiCopy() const;
		bool isSupressDuplicateWarning() const;
		bool isNetwork() const;

		ConfigNode& getProperties();
		const ConfigNode& getProperties() const;

		ConfigNode toConfigNode() const override;

		Vector<String> getMessageNames() const;
		int getMessageNumParams(const String& messageId) const;
		
		static std::shared_ptr<ScriptGraph> loadResource(ResourceLoader& loader);
		constexpr static AssetType getAssetType() { return AssetType::ScriptGraph; }

		void reload(Resource&& resource) override;
		void makeDefault() override;

		void serialize(Serializer& s) const;
		void deserialize(Deserializer& s);

		GraphNodeId addNode(const String& type, Vector2f pos, ConfigNode settings) override;
		void makeBaseGraph();

		OptionalLite<GraphNodeId> getStartNode() const;
		OptionalLite<GraphNodeId> getCallee(GraphNodeId node) const;
		OptionalLite<GraphNodeId> getCaller(GraphNodeId node) const;
		OptionalLite<GraphNodeId> getReturnTo(GraphNodeId node) const;
		OptionalLite<GraphNodeId> getReturnFrom(GraphNodeId node) const;

		std::optional<GraphNodeId> getMessageInboxId(const String& messageId, bool requiresSpawningScript = false) const;

		void finishGraph() override;
		void updateHash() override;
		uint64_t getHash() const;
		uint64_t getAssetHash() const;

		GraphNodeId getNodeRoot(GraphNodeId nodeId) const;
		const ScriptGraphNodeRoots& getRoots() const;
		void setRoots(ScriptGraphNodeRoots roots);

		void appendGraph(GraphNodeId parent, const ScriptGraph& other);
		Vector<int> getSubGraphIndicesForAssetId(const String& id) const;
		Range<GraphNodeId> getSubGraphRange(int subGraphIdx) const;

		FunctionParameters getFunctionParameters() const;

		const ScriptGraph* getPreviousVersion(uint64_t hash) const;

	private:
		Vector<std::pair<GraphNodeId, GraphNodeId>> callerToCallee;
		Vector<std::pair<GraphNodeId, GraphNodeId>> returnToCaller;
		Vector<std::pair<String, Range<GraphNodeId>>> subGraphs;

		ScriptGraphNodeRoots roots;

		ConfigNode properties;

		std::shared_ptr<ScriptGraph> previousVersion;

		GraphNodeId findNodeRoot(GraphNodeId nodeId) const;
		void generateRoots();
		[[nodiscard]] bool isMultiConnection(GraphNodePinType pinType) const override;
	};

	template <>
    class ConfigNodeSerializer<ScriptGraph> {
    public:
		ConfigNode serialize(const ScriptGraph& script, const EntitySerializationContext& context);
		ScriptGraph deserialize(const EntitySerializationContext& context, const ConfigNode& node);
		void deserialize(const EntitySerializationContext& context, const ConfigNode& node, ScriptGraph& target);
    };
}
