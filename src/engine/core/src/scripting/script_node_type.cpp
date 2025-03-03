#include "halley/scripting/script_node_type.h"

#include <cassert>


#include "halley/entity/world.h"
#include "nodes/script_branching.h"
#include "nodes/script_execution_control.h"
#include "nodes/script_logic_gates.h"
#include "nodes/script_audio.h"
#include "nodes/script_entity.h"
#include "nodes/script_flow_gate.h"
#include "nodes/script_input.h"
#include "nodes/script_loop.h"
#include "nodes/script_messaging.h"
#include "nodes/script_network.h"
#include "nodes/script_sprite.h"
#include "nodes/script_node_variables.h"
#include "nodes/script_transform.h"
#include "nodes/script_ui.h"
#include "nodes/script_wait.h"
#include "nodes/script_wait_for.h"
#include "nodes/script_function.h"
#include "nodes/script_meta.h"
#include "nodes/script_lua.h"
using namespace Halley;

String IScriptNodeType::getShortDescription(const ScriptGraphNode& node, const ScriptGraph& graph, GraphPinId elementIdx) const
{
	return getName();
}

String IScriptNodeType::getLargeLabel(const BaseGraphNode& node) const
{
	return "";
}

std::pair<String, Vector<ColourOverride>> IScriptNodeType::getDescription(const BaseGraphNode& node, PinType elementType, uint8_t elementIdx, const BaseGraph& graph) const
{
	switch (ScriptNodeElementType(elementType.type)) {
	case ScriptNodeElementType::ReadDataPin:
	case ScriptNodeElementType::WriteDataPin:
	case ScriptNodeElementType::FlowPin:
	case ScriptNodeElementType::TargetPin:
		return getPinAndConnectionDescription(node, elementType, elementIdx, graph);
	case ScriptNodeElementType::Node:
		return getNodeDescription(node, graph);
	default:
		return { "?", {} };
	}
}

std::pair<String, Vector<ColourOverride>> IScriptNodeType::getPinAndConnectionDescription(const BaseGraphNode& node, PinType elementType, GraphPinId elementIdx, const BaseGraph& graph) const
{
	auto pinDesc = getPinDescription(node, elementType, elementIdx);

	ColourStringBuilder builder;

	const auto type = ScriptNodeElementType(elementType.type);
	if ((type == ScriptNodeElementType::ReadDataPin || type == ScriptNodeElementType::TargetPin) && elementType.direction == GraphNodePinDirection::Input) {
		const auto connected = getConnectedNodeName(node, graph, elementIdx);
		builder.append(pinDesc);
		if (connected != "<empty>") {
			builder.append(" := ");
			builder.append(connected, settingColour);
		}
	} else if (type == ScriptNodeElementType::WriteDataPin && elementType.direction == GraphNodePinDirection::Output) {
		const auto connected = getConnectedNodeName(node, graph, elementIdx);
		if (connected != "<empty>") {
			builder.append(connected, settingColour);
			builder.append(" := ");
		}
		builder.append(pinDesc);
	} else {
		builder.append(pinDesc);
	}

	return builder.moveResults();
}

ConfigNode IScriptNodeType::readDataPin(ScriptEnvironment& environment, const ScriptGraphNode& node, size_t pinN) const
{
	return environment.readInputDataPin(node, static_cast<GraphPinId>(pinN));
}

void IScriptNodeType::writeDataPin(ScriptEnvironment& environment, const ScriptGraphNode& node, size_t pinN, ConfigNode data) const
{
	const auto& pins = node.getPins();
	if (pinN >= pins.size()) {
		return;
	}

	const auto& pin = pins[pinN];
	if (pin.connections.empty() || !pin.connections[0].dstNode) {
		return;
	}
	assert(pin.connections.size() == 1);

	const auto& dst = pin.connections[0];
	const auto& nodes = environment.getCurrentGraph()->getNodes();
	const auto& dstNode = nodes[dst.dstNode.value()];
	dstNode.getNodeType().setData(environment, dstNode, dst.dstPin, std::move(data), environment.getNodeData(dst.dstNode.value()));
}

String IScriptNodeType::getConnectedNodeName(const BaseGraphNode& node, const BaseGraph& graph, size_t pinN) const
{
	const auto& pin = node.getPin(pinN);
	if (pin.connections.empty()) {
		if (dynamic_cast<const ScriptGraphNode&>(node).getNodeType().getPin(node, pinN).type == GraphElementType(ScriptNodeElementType::TargetPin)) {
			return "<current entity>";
		} else {
			return "<empty>";
		}
	}
	assert(pin.connections.size() == 1);

	if (pin.connections[0].dstNode) {
		const auto& otherNode = dynamic_cast<const ScriptGraphNode&>(graph.getNode(pin.connections[0].dstNode.value()));
		return otherNode.getNodeType().getShortDescription(otherNode, dynamic_cast<const ScriptGraph&>(graph), pin.connections[0].dstPin);
	}
	
	return "<unknown>";
}

String IScriptNodeType::getPinTypeName(PinType type) const
{
	switch (static_cast<ScriptNodeElementType>(type.type)) {
	case ScriptNodeElementType::FlowPin:
		return "Flow";
	case ScriptNodeElementType::ReadDataPin:
		return "Read Data";
	case ScriptNodeElementType::WriteDataPin:
		return "Write Data";
	case ScriptNodeElementType::TargetPin:
		return "Target";
	}
	return "?";
}

EntityId IScriptNodeType::readEntityId(ScriptEnvironment& environment, const ScriptGraphNode& node, size_t idx) const
{
	return environment.readInputEntityId(node, static_cast<GraphPinId>(idx));
}

EntityId IScriptNodeType::readRawEntityId(ScriptEnvironment& environment, const ScriptGraphNode& node, size_t idx) const
{
	return environment.readInputEntityIdRaw(node, static_cast<GraphPinId>(idx));
}

std::array<IScriptNodeType::OutputNode, 8> IScriptNodeType::getOutputNodes(const ScriptGraphNode& node, uint8_t outputActiveMask) const
{
	std::array<OutputNode, 8> result;
	result.fill({});
	
	const auto& pinConfig = getPinConfiguration(node);

	size_t curOutputPin = 0;
	size_t nOutputsFound = 0;
	for (size_t i = 0; i < pinConfig.size(); ++i) {
		if (pinConfig[i].type == GraphElementType(ScriptNodeElementType::FlowPin) && pinConfig[i].direction == GraphNodePinDirection::Output) {
			const bool outputActive = (outputActiveMask & (1 << curOutputPin)) != 0;
			if (outputActive) {
				const auto& output = node.getPin(i);
				for (auto& conn: output.connections) {
					if (conn.dstNode) {
						result[nOutputsFound++] = OutputNode{ conn.dstNode, static_cast<GraphPinId>(i), conn.dstPin };
					}
				}
			}

			++curOutputPin;
		}
	}

	return result;
}

GraphPinId IScriptNodeType::getNthOutputPinIdx(const ScriptGraphNode& node, size_t n) const
{
	const auto& pinConfig = getPinConfiguration(node);
	size_t curOutputPin = 0;
	for (size_t i = 0; i < pinConfig.size(); ++i) {
		if (pinConfig[i].type == GraphElementType(ScriptNodeElementType::FlowPin) && pinConfig[i].direction == GraphNodePinDirection::Output) {
			if (curOutputPin == n) {
				return static_cast<GraphPinId>(i);
			}
			++curOutputPin;
		}
	}
	return 0xFF;
}

String IScriptNodeType::addParentheses(String str)
{
	if (str.contains(' ')) {
		return "(" + std::move(str) + ")";
	}
	return str;
}

Colour4f IScriptNodeType::getColour() const
{
	switch (getClassification()) {
	case ScriptNodeClassification::Terminator:
		return Colour4f(0.97f, 0.35f, 0.35f);
	case ScriptNodeClassification::Action:
		return Colour4f(0.07f, 0.84f, 0.09f);
	case ScriptNodeClassification::Variable:
		return Colour4f(0.91f, 0.71f, 0.0f);
	case ScriptNodeClassification::Expression:
		return Colour4f(1.0f, 0.64f, 0.14f);
	case ScriptNodeClassification::FlowControl:
		return Colour4f(0.35f, 0.55f, 0.97f);
	case ScriptNodeClassification::State:
		return Colour4f(0.75f, 0.35f, 0.97f);
	case ScriptNodeClassification::Function:
		return Colour4f(1.00f, 0.49f, 0.68f);
	case ScriptNodeClassification::NetworkFlow:
		return Colour4f(0.15f, 0.85f, 0.98f);
	case ScriptNodeClassification::Comment:
		return Colour4f(0.25f, 0.25f, 0.3f);
	case ScriptNodeClassification::DebugDisplay:
		return Colour4f(0.1f, 0.1f, 0.15f);
	case ScriptNodeClassification::Unknown:
		return Colour4f(0.2f, 0.2f, 0.2f);
	}
	return Colour4f(0.2f, 0.2f, 0.2f);
}

int IScriptNodeType::getSortOrder() const
{
	return static_cast<int>(getClassification());
}

ScriptNodeTypeCollection::ScriptNodeTypeCollection()
{
	addBasicScriptNodes();
}

void ScriptNodeTypeCollection::addScriptNode(std::unique_ptr<IGraphNodeType> nodeType)
{
	addNodeType(std::move(nodeType));
}

const IScriptNodeType* ScriptNodeTypeCollection::tryGetNodeType(const String& typeId) const
{
	return dynamic_cast<const IScriptNodeType*>(tryGetGraphNodeType(typeId));
}

void ScriptNodeTypeCollection::addBasicScriptNodes()
{
	addScriptNode(std::make_unique<ScriptStart>());
	addScriptNode(std::make_unique<ScriptDestructor>());
	addScriptNode(std::make_unique<ScriptRestart>());
	addScriptNode(std::make_unique<ScriptStop>());
	addScriptNode(std::make_unique<ScriptSpinwait>());
	addScriptNode(std::make_unique<ScriptStartScript>());
	addScriptNode(std::make_unique<ScriptStopScript>());
	addScriptNode(std::make_unique<ScriptStopTag>());
	addScriptNode(std::make_unique<ScriptWait>());
	addScriptNode(std::make_unique<ScriptWaitFor>());
	addScriptNode(std::make_unique<ScriptSpriteAnimation>());
	addScriptNode(std::make_unique<ScriptSpriteAnimationState>());
	addScriptNode(std::make_unique<ScriptSpriteDirection>());
	addScriptNode(std::make_unique<ScriptSpriteAlpha>());
	addScriptNode(std::make_unique<ScriptSpriteActionPoint>());
	addScriptNode(std::make_unique<ScriptColourGradient>());
	addScriptNode(std::make_unique<ScriptBranch>());
	addScriptNode(std::make_unique<ScriptMergeAll>());
	addScriptNode(std::make_unique<ScriptLogicGateAnd>());
	addScriptNode(std::make_unique<ScriptLogicGateOr>());
	addScriptNode(std::make_unique<ScriptLogicGateXor>());
	addScriptNode(std::make_unique<ScriptLogicGateNot>());
	addScriptNode(std::make_unique<ScriptAudioEvent>());
	addScriptNode(std::make_unique<ScriptVariable>());
	addScriptNode(std::make_unique<ScriptEntityVariable>());
	addScriptNode(std::make_unique<ScriptLiteral>());
	addScriptNode(std::make_unique<ScriptVariableTable>());
	addScriptNode(std::make_unique<ScriptECSVariable>());
	addScriptNode(std::make_unique<ScriptColourLiteral>());
	addScriptNode(std::make_unique<ScriptComparison>());
	addScriptNode(std::make_unique<ScriptArithmetic>());
	addScriptNode(std::make_unique<ScriptValueOr>());
	addScriptNode(std::make_unique<ScriptConditionalOperator>());
	addScriptNode(std::make_unique<ScriptLerp>());
	addScriptNode(std::make_unique<ScriptAdvanceTo>());
	addScriptNode(std::make_unique<ScriptSetVariable>());
	addScriptNode(std::make_unique<ScriptHoldVariable>());
	addScriptNode(std::make_unique<ScriptInputButton>());
	addScriptNode(std::make_unique<ScriptHasInputLabel>());
	addScriptNode(std::make_unique<ScriptForLoop>());
	addScriptNode(std::make_unique<ScriptForEachLoop>());
	addScriptNode(std::make_unique<ScriptWhileLoop>());
	addScriptNode(std::make_unique<ScriptLerpLoop>());
	addScriptNode(std::make_unique<ScriptEveryFrame>());
	addScriptNode(std::make_unique<ScriptEveryTime>());
	addScriptNode(std::make_unique<ScriptFlowGate>());
	addScriptNode(std::make_unique<ScriptSwitchGate>());
	addScriptNode(std::make_unique<ScriptFlowOnce>());
	addScriptNode(std::make_unique<ScriptLatch>());
	addScriptNode(std::make_unique<ScriptCache>());
	addScriptNode(std::make_unique<ScriptFence>());
	addScriptNode(std::make_unique<ScriptBreaker>());
	addScriptNode(std::make_unique<ScriptSignal>());
	addScriptNode(std::make_unique<ScriptLineReset>());
	addScriptNode(std::make_unique<ScriptDetachFlow>());
	addScriptNode(std::make_unique<ScriptEntityAuthority>());
	addScriptNode(std::make_unique<ScriptHostAuthority>());
	addScriptNode(std::make_unique<ScriptIfEntityAuthority>());
	addScriptNode(std::make_unique<ScriptIfHostAuthority>());
	addScriptNode(std::make_unique<ScriptLock>());
	addScriptNode(std::make_unique<ScriptLockAvailable>());
	addScriptNode(std::make_unique<ScriptLockAvailableGate>());
	addScriptNode(std::make_unique<ScriptTransferToHost>());
	addScriptNode(std::make_unique<ScriptTransferToClient>());
	addScriptNode(std::make_unique<ScriptSendMessage>());
	addScriptNode(std::make_unique<ScriptSendGenericMessage>());
	addScriptNode(std::make_unique<ScriptReceiveMessage>());
	addScriptNode(std::make_unique<ScriptSendSystemMessage>());
	addScriptNode(std::make_unique<ScriptSendEntityMessage>());
	addScriptNode(std::make_unique<ScriptEntityIdToData>());
	addScriptNode(std::make_unique<ScriptDataToEntityId>());
	addScriptNode(std::make_unique<ScriptUIModal>());
	addScriptNode(std::make_unique<ScriptUIInWorld>());
	addScriptNode(std::make_unique<ScriptSetPosition>());
	addScriptNode(std::make_unique<ScriptSetHeight>());
	addScriptNode(std::make_unique<ScriptSetSubworld>());
	addScriptNode(std::make_unique<ScriptGetPosition>());
	addScriptNode(std::make_unique<ScriptGetRotation>());
	addScriptNode(std::make_unique<ScriptSetRotation>());
	addScriptNode(std::make_unique<ScriptSetScale>());
	addScriptNode(std::make_unique<ScriptSpawnEntity>());
	addScriptNode(std::make_unique<ScriptDestroyEntity>());
	addScriptNode(std::make_unique<ScriptFindChildByName>());
	addScriptNode(std::make_unique<ScriptGetParent>());
	addScriptNode(std::make_unique<ScriptEntityReference>());
	addScriptNode(std::make_unique<ScriptEntityParameter>());
	addScriptNode(std::make_unique<ScriptEntityTargetReference>());
	addScriptNode(std::make_unique<ScriptFunctionCallExternal>());
	addScriptNode(std::make_unique<ScriptFunctionReturn>());
	addScriptNode(std::make_unique<ScriptComment>());
	addScriptNode(std::make_unique<ScriptDebugDisplay>());
	addScriptNode(std::make_unique<ScriptLog>());
	addScriptNode(std::make_unique<ScriptHasTags>());
	addScriptNode(std::make_unique<ScriptToVector>());
	addScriptNode(std::make_unique<ScriptFromVector>());
	addScriptNode(std::make_unique<ScriptInsertValueIntoMap>());
	addScriptNode(std::make_unique<ScriptGetValueFromMap>());
	addScriptNode(std::make_unique<ScriptPackMap>());
	addScriptNode(std::make_unique<ScriptUnpackMap>());
	addScriptNode(std::make_unique<ScriptInsertValueIntoSequence>());
	addScriptNode(std::make_unique<ScriptHasSequenceValue>());
	addScriptNode(std::make_unique<ScriptLuaExpression>());
	addScriptNode(std::make_unique<ScriptLuaStatement>());
	addScriptNode(std::make_unique<ScriptToggleEntityEnabled>());
	addScriptNode(std::make_unique<ScriptWaitUntilEndOfFrame>());
}
