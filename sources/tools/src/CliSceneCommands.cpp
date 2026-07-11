#include "CliCommands.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"

#include <optional>
#include <string>

namespace kb::cli {

namespace {

[[nodiscard]] const char* BackendName(kb::scene::BehaviourBackend backend) noexcept {
    switch (backend) {
    case kb::scene::BehaviourBackend::Native:
        return "Native";
    case kb::scene::BehaviourBackend::Lua:
        return "Lua";
    case kb::scene::BehaviourBackend::VisualGraph:
        return "VisualGraph";
    }
    return "Unknown";
}

[[nodiscard]] const char* TickGroupName(kb::scene::BehaviourTickGroup group) noexcept {
    switch (group) {
    case kb::scene::BehaviourTickGroup::Input:
        return "Input";
    case kb::scene::BehaviourTickGroup::Gameplay:
        return "Gameplay";
    case kb::scene::BehaviourTickGroup::Physics:
        return "Physics";
    case kb::scene::BehaviourTickGroup::Animation:
        return "Animation";
    case kb::scene::BehaviourTickGroup::Camera:
        return "Camera";
    case kb::scene::BehaviourTickGroup::Presentation:
        return "Presentation";
    }
    return "Unknown";
}

[[nodiscard]] std::optional<kb::scene::BehaviourTickGroup> ParseTickGroup(std::string_view text) noexcept {
    if (text == "Input") {
        return kb::scene::BehaviourTickGroup::Input;
    }
    if (text == "Gameplay") {
        return kb::scene::BehaviourTickGroup::Gameplay;
    }
    if (text == "Physics") {
        return kb::scene::BehaviourTickGroup::Physics;
    }
    if (text == "Animation") {
        return kb::scene::BehaviourTickGroup::Animation;
    }
    if (text == "Camera") {
        return kb::scene::BehaviourTickGroup::Camera;
    }
    if (text == "Presentation") {
        return kb::scene::BehaviourTickGroup::Presentation;
    }
    return std::nullopt;
}

[[nodiscard]] std::string ComponentSummary(const kb::scene::ScenePrefabNodeDesc& node) {
    std::string summary;
    const auto append = [&summary](const char* name) {
        if (!summary.empty()) {
            summary += ", ";
        }
        summary += name;
    };
    if (node.components.camera.has_value()) {
        append("Camera");
    }
    if (node.components.meshRenderer.has_value()) {
        append("MeshRenderer");
    }
    if (node.components.light.has_value()) {
        append("Light");
    }
    if (node.components.input.has_value()) {
        append("Input");
    }
    if (node.components.rigidbody.has_value()) {
        append("Rigidbody");
    }
    if (node.components.collider.has_value()) {
        append("Collider");
    }
    if (node.components.tags.has_value()) {
        append("Tags");
    }
    if (node.components.audioSource.has_value()) {
        append("AudioSource");
    }
    if (node.components.audioListener.has_value()) {
        append("AudioListener");
    }
    return summary;
}

struct SceneFileResolution {
    bool succeeded = false;
    std::filesystem::path path;
    std::string error;
};

// --scene accepts either a physical path (optionally relative to the project
// root) or a "/Game/..." virtual path resolved through asset discovery.
[[nodiscard]] SceneFileResolution ResolveSceneFile(
    const ArgumentList& arguments,
    kb::scene::Scene* mountedScene) {
    SceneFileResolution resolution;
    const std::optional<std::string> sceneOption = arguments.Option("--scene");
    if (!sceneOption.has_value()) {
        resolution.error = "missing required option --scene <path>";
        return resolution;
    }

    const std::filesystem::path projectRoot = arguments.Option("--project").value_or("");
    if (!sceneOption->empty() && sceneOption->front() == '/') {
        if (mountedScene == nullptr) {
            resolution.error = "virtual scene paths require --project <dir>";
            return resolution;
        }
        const kb::assets::AssetMetadata* metadata =
            FindAssetByFlexiblePath(mountedScene->Assets().Manager().Registry(), *sceneOption);
        if (metadata == nullptr) {
            resolution.error = "scene asset was not found: " + *sceneOption;
            return resolution;
        }
        resolution.path = metadata->physicalPath;
    } else {
        resolution.path = ResolveInputPath(*sceneOption, projectRoot);
    }

    resolution.succeeded = true;
    return resolution;
}

} // namespace

int RunSceneListCommand(const ArgumentList& arguments, CommandIo io) {
    kb::scene::Scene scene;
    kb::scene::Scene* mounted = nullptr;
    if (const std::optional<std::string> project = arguments.Option("--project"); project.has_value()) {
        std::string error;
        if (!MountProjectAssets(scene, *project, error)) {
            io.err << "error: " << error << '\n';
            return 1;
        }
        mounted = &scene;
    }

    const SceneFileResolution resolved = ResolveSceneFile(arguments, mounted);
    if (!resolved.succeeded) {
        io.err << "error: " << resolved.error << '\n';
        return 1;
    }

    const kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(resolved.path);
    if (!loaded.succeeded) {
        io.err << "error: could not load scene: " << loaded.error << '\n';
        return 1;
    }

    io.out << "scene: " << loaded.document.name << " (" << loaded.document.worldPrefab.NodeCount() << " nodes)\n";
    const std::span<const kb::scene::ScenePrefabNodeDesc> nodes = loaded.document.worldPrefab.Nodes();
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const kb::scene::ScenePrefabNodeDesc& node = nodes[index];
        io.out << "  [" << index << "] " << node.name;
        if (node.parentNode != kb::scene::ScenePrefabNodeDesc::NoParent) {
            io.out << " (parent " << node.parentNode << ")";
        }
        const std::string components = ComponentSummary(node);
        if (!components.empty()) {
            io.out << " components: " << components;
        }
        if (node.components.behaviour.has_value()) {
            const kb::scene::BehaviourComponent& behaviour = *node.components.behaviour;
            io.out << " behaviour: ";
            const kb::assets::AssetMetadata* metadata = mounted != nullptr
                ? mounted->Assets().Manager().Registry().Find(kb::assets::AssetId{ behaviour.behaviourAssetId })
                : nullptr;
            if (metadata != nullptr) {
                io.out << metadata->virtualPath.generic_string();
            } else {
                io.out << "0x" << kb::assets::ToString(kb::assets::AssetId{ behaviour.behaviourAssetId });
            }
            io.out << " (" << BackendName(behaviour.backend)
                   << ", " << TickGroupName(behaviour.tickGroup)
                   << ", order " << behaviour.executionOrder
                   << (behaviour.enabled ? "" : ", disabled")
                   << ")";
        }
        io.out << '\n';
    }
    return 0;
}

int RunSceneAttachCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    const std::optional<std::string> nodeName = arguments.Option("--node");
    const std::optional<std::string> scriptPath = arguments.Option("--script");
    if (!project.has_value() || !nodeName.has_value() || !scriptPath.has_value()) {
        io.err << "error: scene-attach requires --project <dir> --scene <path> --node <name> --script <path>\n";
        return 1;
    }

    kb::scene::Scene scene;
    std::string error;
    if (!MountProjectAssets(scene, *project, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }

    const SceneFileResolution resolved = ResolveSceneFile(arguments, &scene);
    if (!resolved.succeeded) {
        io.err << "error: " << resolved.error << '\n';
        return 1;
    }

    const kb::assets::AssetMetadata* metadata =
        FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), ResolveInputPath(*scriptPath, *project));
    if (metadata == nullptr) {
        metadata = FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), *scriptPath);
    }
    if (metadata == nullptr) {
        io.err << "error: script asset was not found: " << *scriptPath
               << " (run with a /Game/... virtual path or a path inside " << *project << "/Assets)\n";
        return 1;
    }

    std::optional<kb::scene::BehaviourComponent> component =
        kb::script::ScriptBehaviourAsset::CreateComponent(*metadata, !arguments.Flag("--disabled"));
    if (!component.has_value()) {
        io.err << "error: asset is not a behaviour (type " << metadata->type << "): "
               << metadata->virtualPath.generic_string() << '\n';
        return 1;
    }

    if (const std::optional<std::string> tickGroup = arguments.Option("--tick-group"); tickGroup.has_value()) {
        const std::optional<kb::scene::BehaviourTickGroup> parsed = ParseTickGroup(*tickGroup);
        if (!parsed.has_value()) {
            io.err << "error: unknown tick group '" << *tickGroup
                   << "' (expected Input, Gameplay, Physics, Animation, Camera, or Presentation)\n";
            return 1;
        }
        component->tickGroup = *parsed;
    }
    if (const std::optional<std::string> order = arguments.Option("--execution-order"); order.has_value()) {
        component->executionOrder = std::stoi(*order);
    }

    kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(resolved.path);
    if (!loaded.succeeded) {
        io.err << "error: could not load scene: " << loaded.error << '\n';
        return 1;
    }

    kb::scene::ScenePrefabNodeDesc* target = nullptr;
    const std::span<const kb::scene::ScenePrefabNodeDesc> nodes = loaded.document.worldPrefab.Nodes();
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (nodes[index].name == *nodeName) {
            target = loaded.document.worldPrefab.TryGetMutableNode(static_cast<std::uint32_t>(index));
            break;
        }
    }
    if (target == nullptr) {
        io.err << "error: node '" << *nodeName << "' was not found in the scene; available nodes:";
        for (const kb::scene::ScenePrefabNodeDesc& node : nodes) {
            io.err << ' ' << node.name;
        }
        io.err << '\n';
        return 1;
    }

    target->components.behaviour = *component;

    if (!kb::scene::SceneDocumentService::Save(loaded.document, resolved.path)) {
        io.err << "error: could not save scene: " << resolved.path.generic_string() << '\n';
        return 1;
    }

    io.out << "attached " << metadata->virtualPath.generic_string()
           << " (" << BackendName(component->backend) << ") to node '" << *nodeName
           << "' in " << resolved.path.generic_string() << '\n';
    return 0;
}

} // namespace kb::cli
