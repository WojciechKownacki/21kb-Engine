#include "scene/EditorSceneContext.hpp"

#include "inspection/ui/InspectorUIClickHandler.hpp"
#include "rendering/script_editor/ScriptSourceFile.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"

namespace kb::editor {

bool EditorSceneContext::SetUIClickHandler(kb::scene::SceneEntity widget, const UIClickChoice* choice) {
    if (!scene_->Entities().IsAlive(widget) || scene_->Components().UI().TryGet<kb::scene::UISelectable>(widget) == nullptr) {
        console_.Warning("Inspector", "On Click needs a widget with a Selectable component.");
        return false;
    }
    if (choice == nullptr) {
        return ExecuteSceneCommand("Clear On Click", [this, widget]() {
            auto selectable = *scene_->Components().UI().TryGet<kb::scene::UISelectable>(widget);
            selectable.eventName.fill('\0');
            selectable.eventTarget = 0U;
            scene_->Components().UI().Set(widget, selectable);
            return true;
        });
    }

    const kb::assets::AssetMetadata* script = scene_->Assets().Manager().Registry().Find(choice->script);
    std::optional<kb::scene::BehaviourComponent> behaviour;
    if (script != nullptr) behaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*script);
    if (!behaviour.has_value() || script->physicalPath.empty()) {
        console_.Warning("Inspector", "On Click needs a Lua script of this project.");
        return false;
    }
    const InspectorUIClickHandler::Target target = InspectorUIClickHandler::FindTarget(*scene_, widget, choice->script);
    if (!target.found) {
        console_.Warning("Inspector", "This widget already runs another script, and no object runs " + script->name +
            ": add " + script->name + " to an object first, then choose its function here.");
        return false;
    }
    if (choice->createFunction) {
        const std::string source = ScriptSourceFile::Read(script->physicalPath);
        if (!ScriptSourceFile::Write(script->physicalPath, source + InspectorUIClickHandler::FunctionTemplate(choice->function))) {
            console_.Error("Scripts", "The function could not be added to " + script->name + ".");
            return false;
        }
        console_.Info("Scripts", "Function " + choice->function + " added to " + script->name + ".");
    }
    return ExecuteSceneCommand("Set On Click", [this, widget, function = choice->function, target, behaviour]() {
        auto selectable = *scene_->Components().UI().TryGet<kb::scene::UISelectable>(widget);
        if (!kb::scene::SetUIEventName(selectable, function)) return false;
        selectable.eventTarget = target.entity.IsValid() ? target.entity.Id() : 0U;
        scene_->Components().UI().Set(widget, selectable);
        if (target.attachToWidget) scene_->Components().Behaviours().Set(widget, *behaviour);
        return true;
    });
}

} // namespace kb::editor
