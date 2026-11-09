#include "engine/script/UISceneValidation.hpp"

#include "engine/script/ScriptLifecycle.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/script/ScriptAsset.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace kb::script {
namespace {

using kb::scene::BehaviourBackend;
using kb::scene::ScenePrefabNodeDesc;

[[nodiscard]] std::string EntityPath(std::span<const ScenePrefabNodeDesc> nodes, std::size_t index) {
    std::vector<std::string_view> parts;
    for (std::size_t at = index; at < nodes.size() && parts.size() < 64U; at = nodes[at].parentNode) {
        parts.push_back(nodes[at].name);
        if (nodes[at].parentNode >= at) break;
    }
    std::string path;
    for (auto part = parts.rbegin(); part != parts.rend(); ++part) {
        if (!path.empty()) path += '/';
        path += *part;
    }
    return path;
}

// The behaviour may answer `function`: it defines it, or it is a script this check cannot read.
[[nodiscard]] bool MayAnswer(const ScenePrefabNodeDesc& node, const UIScriptIndex& scripts, std::string_view function) {
    const auto& behaviour = node.components.behaviour;
    if (!behaviour || !behaviour->enabled || behaviour->behaviourAssetId == 0U) return false;
    if (behaviour->backend != BehaviourBackend::Lua || !scripts.IsLua(behaviour->behaviourAssetId)) return true;
    return scripts.Defines(behaviour->behaviourAssetId, function);
}

[[nodiscard]] bool ThemeMayStyle(const ScenePrefabNodeDesc& node) {
    const auto& rect = node.components.ui.rectTransform;
    return rect && (rect->themeAssetId != 0U || !kb::scene::UIRectThemeOverrides(*rect).empty());
}

} // namespace

UIScriptIndex UIScriptIndex::FromAssets(std::span<const kb::assets::AssetMetadata> assets) {
    UIScriptIndex index;
    for (const kb::assets::AssetMetadata& asset : assets) {
        if (asset.type != ScriptAssetTypes::LuaScript || asset.physicalPath.empty()) continue;
        std::ifstream input(asset.physicalPath, std::ios::binary);
        if (!input.is_open()) continue;
        std::ostringstream buffer;
        buffer << input.rdbuf();
        index.AddLua(asset.id.value, buffer.str());
    }
    return index;
}

void UIScriptIndex::AddLua(std::uint64_t assetId, std::string source) {
    Script script;
    for (LuaGlobalFunction& function : FindLuaGlobalFunctions(source)) script.functions.push_back(std::move(function.name));
    script.source = std::move(source);
    scripts_[assetId] = std::move(script);
}

bool UIScriptIndex::IsLua(std::uint64_t assetId) const noexcept { return scripts_.contains(assetId); }

bool UIScriptIndex::Defines(std::uint64_t assetId, std::string_view function) const noexcept {
    const auto script = scripts_.find(assetId);
    return script != scripts_.end() && std::ranges::find(script->second.functions, function) != script->second.functions.end();
}

bool UIScriptIndex::SubscribesTo(std::string_view eventName) const {
    const std::string doubleQuoted = '"' + std::string{ eventName } + '"';
    const std::string singleQuoted = '\'' + std::string{ eventName } + '\'';
    return std::ranges::any_of(scripts_, [&](const auto& entry) {
        const std::string& source = entry.second.source;
        return source.find("Events.Subscribe") != std::string::npos &&
            (source.find(doubleQuoted) != std::string::npos || source.find(singleQuoted) != std::string::npos);
    });
}

std::vector<UISceneWarning> ValidateUIScene(std::span<const ScenePrefabNodeDesc> nodes, const UIScriptIndex& scripts) {
    std::vector<UISceneWarning> warnings;
    // Per node, parents first: a Canvas on it or above; a widget without one on it or above (reported once,
    // at the top); a theme that may supply what the widget itself lacks.
    std::vector<char> inCanvas(nodes.size(), 0), orphaned(nodes.size(), 0), themed(nodes.size(), 0);
    std::unordered_map<std::uint64_t, std::size_t> byStableId;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (nodes[index].stableId != ScenePrefabNodeDesc::InvalidStableId) byStableId.emplace(nodes[index].stableId, index);
    }
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const ScenePrefabNodeDesc& node = nodes[index];
        const std::size_t parent = node.parentNode < index ? node.parentNode : nodes.size();
        const bool parentCanvas = parent < nodes.size() && inCanvas[parent] != 0;
        const bool parentOrphaned = parent < nodes.size() && orphaned[parent] != 0;
        const kb::scene::UIComponentSet& ui = node.components.ui;
        inCanvas[index] = parentCanvas || ui.canvas.has_value();
        themed[index] = (parent < nodes.size() && themed[parent] != 0) || ThemeMayStyle(node);
        const bool widget = !ui.Empty();
        orphaned[index] = parentOrphaned || (widget && inCanvas[index] == 0);
        const char* kind = ui.button || ui.selectable ? "Button" : (ui.text ? "Text" : "Widget");
        const std::string path = EntityPath(nodes, index);

        if (widget && inCanvas[index] == 0 && !parentOrphaned) {
            warnings.push_back(UISceneWarning{ std::string{ kUIWarningNoCanvas }, path,
                std::string{ kind } + " is not inside a Canvas, so it is never drawn or clicked; put it under a Canvas "
                    "(add the Canvas component to an ancestor)" });
        }
        if (ui.text && ui.text->fontAssetId == 0U && themed[index] == 0 &&
            (!kb::scene::UITextContent(*ui.text).empty() || !kb::scene::UITextLocalizationKey(*ui.text).empty())) {
            warnings.push_back(UISceneWarning{ std::string{ kUIWarningTextNoFont }, path,
                "Text has no font, so nothing is drawn; set its font (Text fontAssetId) to a font asset" });
        }

        const std::string_view eventName = ui.selectable ? kb::scene::UIEventName(*ui.selectable) : std::string_view{};
        if (!eventName.empty()) {
            const ScenePrefabNodeDesc* target = &node;
            bool targetKnown = true;
            if (ui.selectable->eventTarget != 0U) {
                const auto found = byStableId.find(ui.selectable->eventTarget);
                targetKnown = found != byStableId.end();
                if (targetKnown) target = &nodes[found->second];
            }
            // The action reaches the target as an event of that name, and the clicked widget as OnUIClicked
            // with the name in event.args.action.
            if (targetKnown && !MayAnswer(*target, scripts, eventName) && !MayAnswer(node, scripts, "OnUIClicked") &&
                !scripts.SubscribesTo(eventName)) {
                const std::string name{ eventName };
                const bool identifier = std::ranges::all_of(name, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }) &&
                    std::isdigit(static_cast<unsigned char>(name.front())) == 0;
                warnings.push_back(UISceneWarning{ std::string{ kUIWarningEventNoHandler }, path,
                    "event name \"" + name + "\" is sent on click, but no script answers it; define " +
                        (identifier ? "function " + name + "(self, event) on the " + (target == &node ? "widget" : "event target") + " or " : std::string{}) +
                        "OnUIClicked(self, event) on the widget (the name arrives in event.args.action)" });
            }
        } else if (ui.button && !MayAnswer(node, scripts, "OnUIClicked") && !MayAnswer(node, scripts, "OnUIPressed") &&
                   !MayAnswer(node, scripts, "OnUIReleased") && !scripts.SubscribesTo("OnUIClicked") &&
                   !scripts.SubscribesTo("OnUIPressed") && !scripts.SubscribesTo("OnUIReleased")) {
            warnings.push_back(UISceneWarning{ std::string{ kUIWarningButtonNoReaction }, path,
                "Button does nothing when clicked: it has no event name and no script on it defines OnUIClicked(self, event)" });
        }
    }
    return warnings;
}

UIProjectReport ValidateUIProject(std::span<const kb::assets::AssetMetadata> assets, const std::filesystem::path& onlyScene) {
    UIProjectReport report;
    const UIScriptIndex scripts = UIScriptIndex::FromAssets(assets);
    for (const kb::assets::AssetMetadata& asset : assets) {
        if (asset.type != "Scene" || asset.physicalPath.empty()) continue;
        std::error_code same;
        if (!onlyScene.empty() && !std::filesystem::equivalent(asset.physicalPath, onlyScene, same)) continue;
        const std::string scene = kb::assets::NormalizeAssetPath(asset.virtualPath);
        const kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(asset.physicalPath);
        if (!loaded.succeeded) {
            report.errors.push_back(scene + ": could not load the scene: " + loaded.error);
            continue;
        }
        for (UISceneWarning& warning : ValidateUIScene(loaded.document.worldPrefab.Nodes(), scripts)) {
            report.warnings.push_back(UIProjectWarning{ scene, std::move(warning) });
        }
    }
    return report;
}

std::string FormatUIWarning(std::string_view scene, const UISceneWarning& warning) {
    return "WARN " + std::string{ scene } + ':' + warning.entity + ": [" + warning.code + "] " + warning.message;
}

} // namespace kb::script
