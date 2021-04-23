#include "scripting_gizmo.h"

#include <components/script_component.h>


#include "halley/entity/components/transform_2d_component.h"
using namespace Halley;

ScriptingGizmo::ScriptingGizmo(SnapRules snapRules, UIFactory& factory, ISceneEditorWindow& sceneEditorWindow, std::shared_ptr<ScriptNodeTypeCollection> scriptNodeTypes)
	: SceneEditorGizmo(snapRules)
	, factory(factory)
	, sceneEditorWindow(sceneEditorWindow)
	, scriptNodeTypes(std::move(scriptNodeTypes))
{
}

void ScriptingGizmo::update(Time time, const ISceneEditor& sceneEditor, const SceneEditorInputState& inputState)
{
	if (!renderer) {
		renderer = std::make_shared<ScriptRenderer>(sceneEditor.getResources(), sceneEditor.getWorld(), *scriptNodeTypes, sceneEditorWindow.getProjectDefaultZoom());
	}

	const auto* transform = getComponent<Transform2DComponent>();
	basePos = transform ? transform->getGlobalPosition() : Vector2f();

	auto* script = getComponent<ScriptComponent>();
	scriptGraph = script ? &script->scriptGraph : nullptr;
	renderer->setGraph(scriptGraph);

	nodeUnderMouse = renderer->getNodeIdxUnderMouse(basePos, getZoom(), inputState.mousePos);
}

void ScriptingGizmo::draw(Painter& painter) const
{
	if (!renderer) {
		return;
	}

	renderer->setHighlight(nodeUnderMouse);
	renderer->draw(painter, basePos, getZoom());
}

bool ScriptingGizmo::isHighlighted() const
{
	return !!nodeUnderMouse;
}

std::shared_ptr<UIWidget> ScriptingGizmo::makeUI()
{
	// TODO
	return {};
}

std::vector<String> ScriptingGizmo::getHighlightedComponents() const
{
	return { "Script" };
}
