#include "inspection/InspectorPanelTextBuilder.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"

#include "inspection/InspectorCameraTextBuilder.hpp"
#include "inspection/InspectorAudioMixerAssetModel.hpp"
#include "inspection/InspectorAudioTextBuilder.hpp"
#include "inspection/InspectorColliderTextBuilder.hpp"
#include "inspection/InspectorEntitySummaryTextBuilder.hpp"
#include "inspection/InspectorLightTextBuilder.hpp"
#include "inspection/InspectorMaterialTextureSlotFormatter.hpp"
#include "inspection/InspectorMeshRendererTextBuilder.hpp"
#include "inspection/InspectorMultiSelectionTextBuilder.hpp"
#include "inspection/InspectorRigidbodyTextBuilder.hpp"
#include "rendering/InspectorAudioMixerAssetView.hpp"
#include "inspection/InspectorSceneAudioModel.hpp"

#include <array>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>

namespace kb::editor {
namespace {

[[nodiscard]] std::string Normalize(const std::filesystem::path& path) {
    return kb::assets::NormalizeAssetPath(path);
}

[[nodiscard]] std::string FormatFloat(float value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(value));
    std::string text = buffer;
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text == "-0" ? "0" : text;
}

[[nodiscard]] std::string AlphaModeName(kb::render::RenderMaterialAlphaMode mode) {
    switch (mode) {
    case kb::render::RenderMaterialAlphaMode::Opaque:
        return "Opaque";
    case kb::render::RenderMaterialAlphaMode::Mask:
        return "Mask";
    case kb::render::RenderMaterialAlphaMode::Blend:
        return "Blend";
    }
    return "Opaque";
}

struct MaterialTextureTextSlot {
    std::string_view label;
    std::uint64_t assetId = 0U;
};

void AppendMaterialInspectorText(std::ostringstream& text, const EditorSceneContext& sceneContext, kb::assets::AssetId id) {
    const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext.ReadMaterialAsset(id);
    if (!material.has_value()) {
        text << '\n' << "Material: failed to load" << '\n';
        return;
    }
    const kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const std::array<MaterialTextureTextSlot, 5U> textureSlots{ {
        { "Albedo", material->desc.albedoTextureAssetId },
        { "Normal", material->desc.normalTextureAssetId },
        { "Metallic-Roughness", material->desc.metallicRoughnessTextureAssetId },
        { "Occlusion", material->desc.occlusionTextureAssetId },
        { "Emissive", material->desc.emissiveTextureAssetId },
    } };

    text << '\n'
         << "Material" << '\n'
         << "Base Color: "
         << FormatFloat(material->desc.baseColor[0]) << ", "
         << FormatFloat(material->desc.baseColor[1]) << ", "
         << FormatFloat(material->desc.baseColor[2]) << ", "
         << FormatFloat(material->desc.baseColor[3]) << '\n'
         << "Metallic: " << FormatFloat(material->desc.metallicFactor) << '\n'
         << "Roughness: " << FormatFloat(material->desc.roughnessFactor) << '\n'
         << "Normal Scale: " << FormatFloat(material->desc.normalScale) << '\n'
         << "Occlusion Strength: " << FormatFloat(material->desc.occlusionStrength) << '\n'
         << "Emissive Color: "
         << FormatFloat(material->desc.emissiveColor[0]) << ", "
         << FormatFloat(material->desc.emissiveColor[1]) << ", "
         << FormatFloat(material->desc.emissiveColor[2]) << '\n'
         << "Emissive Strength: " << FormatFloat(material->desc.emissiveStrength) << '\n'
         << "Alpha Cutoff: " << FormatFloat(material->desc.alphaCutoff) << '\n'
         << "Alpha Mode: " << AlphaModeName(material->desc.alphaMode) << '\n'
         << "Double Sided: " << (material->desc.doubleSided ? "true" : "false") << '\n';

    bool hasMissingTexture = false;
    for (const MaterialTextureTextSlot& slot : textureSlots) {
        text << slot.label << " Texture: " << InspectorMaterialTextureSlotFormatter::DisplayName(manager, slot.assetId) << '\n';
        hasMissingTexture = hasMissingTexture || InspectorMaterialTextureSlotFormatter::IsMissing(manager, slot.assetId);
    }
    if (hasMissingTexture) {
        text << "Material Texture Diagnostics" << '\n';
        for (const MaterialTextureTextSlot& slot : textureSlots) {
            if (InspectorMaterialTextureSlotFormatter::IsMissing(manager, slot.assetId)) {
                text << "- " << InspectorMaterialTextureSlotFormatter::Diagnostic(slot.label, slot.assetId) << '\n';
            }
        }
    }
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
        if (metadata->type == "RenderMaterial") {
            AppendMaterialInspectorText(text, sceneContext, metadata->id);
        }
        if (InspectorAudioMixerAssetView::Supports(*metadata)) {
            kb::assets::AssetManager& mutableManager = const_cast<kb::assets::AssetManager&>(manager);
            const kb::assets::AssetHandle<kb::audio::AudioMixerAsset> mixer =
                InspectorAudioMixerAssetView::LoadCached(mutableManager, metadata->id);
            text << '\n';
            if (mixer.IsLoaded()) {
                text << InspectorAudioMixerAssetModel{ *mixer }.Text();
            } else {
                text << "Audio Mixer: failed to load" << '\n';
            }
        }
        return text.str();
    }

    return std::nullopt;
}

} // namespace

std::optional<std::string> InspectorPanelTextBuilder::Build(const EditorSceneContext& sceneContext) const {
    if (std::optional<std::string> assetText = BuildAssetInspectorText(sceneContext)) {
        return assetText;
    }

    if (sceneContext.SelectedHierarchyEntities().size() > 1U) {
        return InspectorMultiSelectionTextBuilder{}.Build(sceneContext);
    }

    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();
    if (!sceneContext.Scene().Entities().IsAlive(selected)) {
        return InspectorSceneAudioModel::ShouldDisplay(
                   sceneContext.Scene(), sceneContext.AssetBrowser().InspectorAsset(), selected)
            ? std::optional<std::string>{ InspectorSceneAudioModel{ sceneContext.Scene() }.Text() }
            : std::nullopt;
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
    if (const kb::scene::Animator* animator = sceneContext.Scene().Components().Animators().TryGet(selected); animator != nullptr) {
        text += "\n\nAnimator\nController: " + std::to_string(animator->controllerAssetId) +
            "\nSpeed: " + std::to_string(animator->speed) +
            "\nEnabled: " + std::string{ animator->enabled ? "true" : "false" };
    }
    if (const kb::scene::SkeletonBindingComponent* binding = sceneContext.Scene().Components().SkeletonBindings().TryGet(selected); binding != nullptr) {
        text += "\n\nSkeleton Binding\nSkeleton: " + std::to_string(binding->skeletonAssetId) +
            "\nEnabled: " + std::string{ binding->enabled ? "true" : "false" };
    }
    if (const kb::scene::DrawD3DeformedGeometryComponent* geometry = sceneContext.Scene().Components().DeformedGeometries().TryGet(selected); geometry != nullptr) {
        text += "\n\nDeformed Geometry\nSkeletal Mesh: " + std::to_string(geometry->skeletalMeshAssetId) +
            "\nCasts Shadow: " + std::string{ geometry->castsShadow ? "true" : "false" } +
            "\nReceives Shadow: " + std::string{ geometry->receivesShadow ? "true" : "false" } +
            "\nEnabled: " + std::string{ geometry->enabled ? "true" : "false" };
    }
    if (const kb::scene::UIDocumentComponent* document = sceneContext.Scene().Components().UIDocuments().TryGet(selected); document != nullptr) {
        text += "\n\nUI Document\nDocument: " + std::to_string(document->documentAssetId) +
            "\nEnabled: " + std::string{ document->enabled ? "true" : "false" };
    }
    if (const kb::scene::NavAgent* agent = sceneContext.Scene().Components().NavAgents().TryGet(selected); agent != nullptr) {
        text += "\n\nNav Agent\nRadius: " + std::to_string(agent->radius) +
            "\nMax Speed: " + std::to_string(agent->maxSpeed) +
            "\nArea Mask: " + std::to_string(agent->areaMask) +
            "\nEnabled: " + std::string{ agent->enabled ? "true" : "false" };
    }
    if (const kb::scene::NavObstacle* obstacle = sceneContext.Scene().Components().NavObstacles().TryGet(selected); obstacle != nullptr) {
        text += "\n\nNav Obstacle\nRadius: " + std::to_string(obstacle->radius) +
            "\nArea: " + std::to_string(obstacle->area) +
            "\nCarve: " + std::string{ obstacle->carve ? "true" : "false" };
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
