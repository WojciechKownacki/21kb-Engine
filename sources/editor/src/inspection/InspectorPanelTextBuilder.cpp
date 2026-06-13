#include "inspection/InspectorPanelTextBuilder.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"

#include "inspection/InspectorCameraTextBuilder.hpp"
#include "inspection/InspectorAudioTextBuilder.hpp"
#include "inspection/InspectorColliderTextBuilder.hpp"
#include "inspection/InspectorEntitySummaryTextBuilder.hpp"
#include "inspection/InspectorLightTextBuilder.hpp"
#include "inspection/InspectorMeshRendererTextBuilder.hpp"
#include "inspection/InspectorRigidbodyTextBuilder.hpp"

#include <sstream>

namespace kb::editor {
namespace {

[[nodiscard]] std::string Normalize(const std::filesystem::path& path) {
    return kb::assets::NormalizeAssetPath(path);
}

[[nodiscard]] std::optional<std::string> BuildAssetInspectorText(const EditorSceneContext& sceneContext) {
    const EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    std::ostringstream text;

    if (state.InspectorAsset().IsValid()) {
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(state.InspectorAsset());
        if (metadata == nullptr) {
            return std::nullopt;
        }

        const std::filesystem::path resolved = metadata->physicalPath.empty()
            ? manager.Mounts().Resolve(metadata->virtualPath).value_or(std::filesystem::path{})
            : metadata->physicalPath;
        text << "Asset: " << metadata->name << '\n'
             << "Type: " << metadata->type << '\n'
             << "Category: " << (metadata->importCategory.empty() ? "-" : metadata->importCategory) << '\n'
             << "Id: " << metadata->id.value << '\n'
             << "Virtual path: " << Normalize(metadata->virtualPath) << '\n';
        if (!resolved.empty()) {
            text << "Physical path: " << resolved.string() << '\n';
        }
        text << "Content hash: " << metadata->contentHash << '\n'
             << "Runtime loadable: " << (metadata->runtimeLoadable ? "true" : "false") << '\n'
             << "Loaded: " << (manager.IsLoaded(metadata->id) ? "true" : "false");
        return text.str();
    }

    return std::nullopt;
}

} // namespace

std::optional<std::string> InspectorPanelTextBuilder::Build(const EditorSceneContext& sceneContext) const {
    if (std::optional<std::string> assetText = BuildAssetInspectorText(sceneContext)) {
        return assetText;
    }

    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.Scene().Entities().IsAlive(selected)) {
        return std::nullopt;
    }

    std::string text = InspectorEntitySummaryTextBuilder{}.Build(sceneContext, selected);

    if (const kb::scene::CameraComponent* camera = sceneContext.Scene().Components().Cameras().TryGet(selected); camera != nullptr) {
        InspectorCameraTextBuilder{}.Append(text, *camera);
    }

    if (const kb::scene::MeshRendererComponent* renderer = sceneContext.Scene().Components().MeshRenderers().TryGet(selected); renderer != nullptr) {
        InspectorMeshRendererTextBuilder{}.Append(text, *renderer);
    }

    if (const kb::scene::LightComponent* light = sceneContext.Scene().Components().Lights().TryGet(selected); light != nullptr) {
        InspectorLightTextBuilder{}.Append(text, *light);
    }

    if (const kb::scene::AudioSourceComponent* audioSource = sceneContext.Scene().Components().AudioSources().TryGet(selected); audioSource != nullptr) {
        InspectorAudioSourceTextBuilder{}.Append(text, *audioSource);
    }

    if (const kb::scene::AudioListenerComponent* audioListener = sceneContext.Scene().Components().AudioListeners().TryGet(selected); audioListener != nullptr) {
        InspectorAudioListenerTextBuilder{}.Append(text, *audioListener);
    }

    if (const kb::scene::RigidbodyComponent* rigidbody = sceneContext.Scene().Components().Rigidbodies().TryGet(selected); rigidbody != nullptr) {
        InspectorRigidbodyTextBuilder{}.Append(text, *rigidbody);
    }

    if (const kb::scene::ColliderComponent* collider = sceneContext.Scene().Components().Colliders().TryGet(selected); collider != nullptr) {
        InspectorColliderTextBuilder{}.Append(text, *collider);
    }

    return text;
}

} // namespace kb::editor
