#pragma once

#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/ScenePrefabNode.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene::SceneComponentAssetReferences {

// The single source of truth for "which component fields of a scene or prefab
// node name a registered asset". SceneAssetWriter records them into a scene's
// ".meta" sidecar and ScenePrefabAssetLoader turns them into a prefab asset's
// dependency edges, so both graphs cover exactly the same field set: a component
// field added here reaches the scene sidecar and the prefab graph in one change,
// instead of the two lists drifting apart and quietly dropping an asset the
// cooker then leaves out of the package.
//
// `sink(rawAssetId, role)` is invoked once per authored reference in a fixed
// order. Zero ids are passed through unchanged - "no asset selected" is a
// legitimate authored value and the caller, not this walker, decides what an
// empty reference means. The role strings are persisted verbatim in the ".meta"
// sidecar, so they are part of that file's contract and must not be renamed.
template <typename Sink>
void ForEachReference(const ScenePrefabNodeComponents& components, Sink&& sink) {
    if (components.meshRenderer.has_value()) {
        sink(components.meshRenderer->meshAssetId, std::string_view{ "mesh" });
        sink(components.meshRenderer->materialAssetId, std::string_view{ "material" });
        for (std::uint32_t slot = 0U;
             slot < components.meshRenderer->materialSlotOverrideCount && slot < kMaxMeshRendererMaterialSlotOverrides;
             ++slot) {
            sink(components.meshRenderer->materialSlotAssetIds[slot], std::string_view{ "materialSlot" });
        }
    }
    if (components.input.has_value()) {
        sink(components.input->mappingContextAssetId, std::string_view{ "inputMappingContext" });
    }
    if (components.contentInstance.has_value()) {
        sink(components.contentInstance->assetId, std::string_view{ "contentInstance" });
    }
    if (components.worldBackdrop.has_value()) {
        sink(components.worldBackdrop->environmentAssetId, std::string_view{ "environment" });
    }
    if (components.ambientRadiance.has_value()) {
        sink(components.ambientRadiance->environmentAssetId, std::string_view{ "environment" });
    }
    if (components.geometrySwarm.has_value()) {
        sink(components.geometrySwarm->meshAssetId, std::string_view{ "geometrySwarmMesh" });
        sink(components.geometrySwarm->materialAssetId, std::string_view{ "geometrySwarmMaterial" });
    }
    if (components.surfaceCast.has_value()) {
        sink(components.surfaceCast->materialAssetId, std::string_view{ "surfaceCastMaterial" });
    }
    if (components.spaceStroke.has_value()) {
        sink(components.spaceStroke->meshAssetId, std::string_view{ "spaceStrokeMesh" });
        sink(components.spaceStroke->materialAssetId, std::string_view{ "spaceStrokeMaterial" });
    }
    if (components.historyRibbon.has_value()) {
        sink(components.historyRibbon->meshAssetId, std::string_view{ "historyRibbonMesh" });
        sink(components.historyRibbon->materialAssetId, std::string_view{ "historyRibbonMaterial" });
    }
    if (components.particleEffect.has_value()) {
        sink(components.particleEffect->effectAssetId, std::string_view{ "particleEffect" });
    }
    if (components.lensEcho.has_value()) {
        sink(components.lensEcho->profileMaterialAssetId, std::string_view{ "lensEchoMaterial" });
    }
    if (components.behaviour.has_value()) {
        sink(components.behaviour->behaviourAssetId, std::string_view{ "behaviour" });
    }
    if (components.audioSource.has_value()) {
        sink(components.audioSource->clipAssetId, std::string_view{ "audioClip" });
    }
    if (components.animator.has_value()) {
        sink(components.animator->controllerAssetId, std::string_view{ "animatorController" });
    }
    if (components.skeletonBinding.has_value()) {
        sink(components.skeletonBinding->skeletonAssetId, std::string_view{ "skeleton" });
    }
    if (components.deformedGeometry.has_value()) {
        sink(components.deformedGeometry->skeletalMeshAssetId, std::string_view{ "skeletalMesh" });
        for (std::uint32_t slot = 0U;
             slot < components.deformedGeometry->materialSlotOverrideCount &&
             slot < kMaxDeformedGeometryMaterialSlotOverrides;
             ++slot) {
            sink(components.deformedGeometry->materialSlotAssetIds[slot], std::string_view{ "skeletalMaterialSlot" });
        }
    }
    if (components.uiDocument.has_value()) {
        sink(components.uiDocument->documentAssetId, std::string_view{ "uiDocument" });
    }
}

} // namespace kb::scene::SceneComponentAssetReferences
