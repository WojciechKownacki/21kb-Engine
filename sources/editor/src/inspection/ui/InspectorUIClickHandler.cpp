#include "inspection/ui/InspectorUIClickHandler.hpp"

#include "localization/EditorLocalization.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "engine/script/ScriptLifecycle.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>

namespace kb::editor {
namespace {

[[nodiscard]] std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

// A function the engine itself calls or a UI callback: not something a click is wired to.
[[nodiscard]] bool IsHook(const std::string& name) {
    static constexpr std::array<std::string_view, 10> kLifecycle{ "Created", "Activated", "Ready", "FixedTick", "Tick",
        "LateTick", "BeforeRender", "AfterRender", "Deactivated", "Destroyed" };
    return name.starts_with("OnUI") || std::ranges::find(kLifecycle, name) != kLifecycle.end();
}

[[nodiscard]] std::string SuggestedName(const kb::scene::Scene& scene, kb::scene::SceneEntity widget,
    const std::vector<std::string>& taken) {
    std::string base;
    for (const char c : scene.Entities().Name(widget))
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) base.push_back(c);
    if (base.empty() || std::isdigit(static_cast<unsigned char>(base.front())) != 0) base = "Button" + base;
    if (!base.ends_with("Clicked")) base += "Clicked";
    std::string name = base;
    for (int suffix = 2; std::ranges::find(taken, name) != taken.end(); ++suffix) name = base + std::to_string(suffix);
    return name;
}

struct HolderSearch {
    kb::assets::AssetId script{};
    std::vector<kb::scene::SceneEntity> holders;
};

} // namespace

std::vector<UIClickChoice> InspectorUIClickHandler::Choices(const kb::scene::Scene& scene, kb::scene::SceneEntity widget) {
    std::vector<const kb::assets::AssetMetadata*> scripts;
    for (const kb::assets::AssetMetadata& asset : scene.Assets().Manager().Registry().All())
        if (asset.type == kb::script::ScriptAssetTypes::LuaScript && !asset.physicalPath.empty()) scripts.push_back(&asset);
    std::ranges::sort(scripts, [](const auto* lhs, const auto* rhs) {
        return lhs->name != rhs->name ? lhs->name < rhs->name : lhs->id.value < rhs->id.value;
    });
    std::vector<UIClickChoice> choices;
    for (const kb::assets::AssetMetadata* script : scripts) {
        std::vector<std::string> taken;
        for (const kb::script::LuaGlobalFunction& function : kb::script::FindLuaGlobalFunctions(ReadFile(script->physicalPath))) {
            taken.push_back(function.name);
            if (!IsHook(function.name)) choices.push_back({ script->id, script->name, function.name, false });
        }
        choices.push_back({ script->id, script->name, SuggestedName(scene, widget, taken), true });
    }
    return choices;
}

std::string InspectorUIClickHandler::Describe(const kb::scene::Scene& scene, kb::scene::SceneEntity widget) {
    const auto* selectable = scene.Components().UI().TryGet<kb::scene::UISelectable>(widget);
    const std::string_view event = selectable != nullptr ? kb::scene::UIEventName(*selectable) : std::string_view{};
    if (selectable == nullptr || event.empty()) return std::string{ EditorText("inspector.onClick.none", "(none)") };
    const kb::scene::SceneEntity target = selectable->eventTarget != 0U ? kb::scene::SceneEntity{ selectable->eventTarget } : widget;
    if (!scene.Entities().IsAlive(target))
        return std::string{ event } + " " + std::string{ EditorText("inspector.onClick.targetMissing", "(target missing)") };
    const auto* behaviour = scene.Components().Behaviours().TryGet(target);
    const kb::assets::AssetMetadata* script = behaviour != nullptr
        ? scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ behaviour->behaviourAssetId }) : nullptr;
    if (script != nullptr && !script->physicalPath.empty()) {
        const auto functions = kb::script::FindLuaGlobalFunctions(ReadFile(script->physicalPath));
        if (std::ranges::any_of(functions, [event](const auto& function) { return function.name == event; }))
            return script->name + "." + std::string{ event };
    }
    return std::string{ event } + " " + std::string{ EditorText("inspector.onClick.noScript", "(no script answers)") };
}

std::string InspectorUIClickHandler::FunctionTemplate(const std::string& function) {
    return "\nfunction " + function + "(self, event)\n    -- Runs when the button is clicked.\nend\n";
}

InspectorUIClickHandler::Target InspectorUIClickHandler::FindTarget(
    const kb::scene::Scene& scene, kb::scene::SceneEntity widget, kb::assets::AssetId script) {
    const auto* own = scene.Components().Behaviours().TryGet(widget);
    if (own != nullptr && own->behaviourAssetId == script.value) return { true, {}, false };
    HolderSearch search{ script, {} };
    scene.Components().Behaviours().ForEach([](kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour, void* context) {
        auto& found = *static_cast<HolderSearch*>(context);
        if (behaviour.behaviourAssetId == found.script.value) found.holders.push_back(entity);
    }, &search);
    // The nearest ancestor that runs the script, else the first object that does.
    for (auto ancestor = scene.Hierarchy().Parent(widget); ancestor.IsValid(); ancestor = scene.Hierarchy().Parent(ancestor))
        if (std::ranges::find(search.holders, ancestor) != search.holders.end()) return { true, ancestor, false };
    if (!search.holders.empty()) return { true, search.holders.front(), false };
    if (own == nullptr) return { true, {}, true };
    return {};
}

} // namespace kb::editor
