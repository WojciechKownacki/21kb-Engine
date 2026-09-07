#pragma once

#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/ui/UIComponentPropertyCatalog.hpp"
#include "scene/ui/SceneUIComponentTextCodec.hpp"

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace kb::scene::SceneComponentAssetReferences {

// The single source of truth for "which component fields of a scene or prefab
// node name a registered asset". SceneAssetWriter records them into a scene's
// ".meta" sidecar and ScenePrefabAssetLoader turns them into a prefab asset's
// dependency edges, so both graphs cover exactly the same field set: a component
// field added here reaches the scene sidecar and the prefab graph in one change,
// instead of the two lists drifting apart and quietly dropping an asset the
// cooker then leaves out of the package.
//
// `sink(rawAssetId, role, uiProperty)` is invoked once per authored reference in a fixed
// order. Zero ids are passed through unchanged - "no asset selected" is a
// legitimate authored value and the caller, not this walker, decides what an
// empty reference means. The role strings are persisted verbatim in the ".meta"
// sidecar, so they are part of that file's contract and must not be renamed.
// `uiProperty` is non-null only for a typed UI asset reference; it points into
// UIComponentPropertyCatalog, which owns both the dependency role and expected
// asset kind used by save, cook, Inspector and scripts.
template <typename Sink>
void ForEachUIReference(const UIComponentSet& components, Sink&& sink) {
    for (const UIComponentDescriptor& component : UIComponentCatalog()) {
        if (!HasUIComponent(components, component.type)) {
            continue;
        }
        for (const UIComponentPropertyDescriptor& property : UIComponentPropertyCatalog(component.type)) {
            if (property.type != UIComponentPropertyType::Asset) {
                continue;
            }
            UIComponentPropertyValue value{false};
            if (ReadUIComponentProperty(components, component.type, property.name, value)) {
                sink(std::get<std::uint64_t>(value), property.assetDependencyRole, &property);
            }
        }
    }
}

template <typename Sink>
[[nodiscard]] bool ForEachUIOverrideReference(
    const std::vector<ScenePrefabPropertyOverride>& overrides,
    Sink&& sink) {
    for (const ScenePrefabPropertyOverride& property : overrides) {
        if (property.propertyPath != "ui") {
            continue;
        }
        UIComponentSet components;
        if (!SceneUIComponentTextCodec::Decode(property.value, components)) {
            return false;
        }
        ForEachUIReference(components, sink);
    }
    return true;
}

template <typename Sink>
void ForEachReference(const ScenePrefabNodeComponents& components, Sink&& sink) {
    if (components.meshRenderer.has_value()) {
        sink(components.meshRenderer->meshAssetId, std::string_view{ "mesh" }, nullptr);
        sink(components.meshRenderer->materialAssetId, std::string_view{ "material" }, nullptr);
        for (std::uint32_t slot = 0U;
             slot < components.meshRenderer->materialSlotOverrideCount && slot < kMaxMeshRendererMaterialSlotOverrides;
             ++slot) {
            sink(components.meshRenderer->materialSlotAssetIds[slot], std::string_view{ "materialSlot" }, nullptr);
        }
    }
    if (components.input.has_value()) {
        sink(components.input->mappingContextAssetId, std::string_view{ "inputMappingContext" }, nullptr);
    }
    if (components.contentInstance.has_value()) {
        sink(components.contentInstance->assetId, std::string_view{ "contentInstance" }, nullptr);
    }
    if (components.worldBackdrop.has_value()) {
        sink(components.worldBackdrop->environmentAssetId, std::string_view{ "environment" }, nullptr);
    }
    if (components.ambientRadiance.has_value()) {
        sink(components.ambientRadiance->environmentAssetId, std::string_view{ "environment" }, nullptr);
    }
    if (components.geometrySwarm.has_value()) {
        sink(components.geometrySwarm->meshAssetId, std::string_view{ "geometrySwarmMesh" }, nullptr);
        sink(components.geometrySwarm->materialAssetId, std::string_view{ "geometrySwarmMaterial" }, nullptr);
    }
    if (components.surfaceCast.has_value()) {
        sink(components.surfaceCast->materialAssetId, std::string_view{ "surfaceCastMaterial" }, nullptr);
    }
    if (components.spaceStroke.has_value()) {
        sink(components.spaceStroke->meshAssetId, std::string_view{ "spaceStrokeMesh" }, nullptr);
        sink(components.spaceStroke->materialAssetId, std::string_view{ "spaceStrokeMaterial" }, nullptr);
    }
    if (components.historyRibbon.has_value()) {
        sink(components.historyRibbon->meshAssetId, std::string_view{ "historyRibbonMesh" }, nullptr);
        sink(components.historyRibbon->materialAssetId, std::string_view{ "historyRibbonMaterial" }, nullptr);
    }
    if (components.particleEffect.has_value()) {
        sink(components.particleEffect->effectAssetId, std::string_view{ "particleEffect" }, nullptr);
    }
    ForEachUIReference(components.ui, sink);
    if (components.lensEcho.has_value()) {
        sink(components.lensEcho->profileMaterialAssetId, std::string_view{ "lensEchoMaterial" }, nullptr);
    }
    if (components.behaviour.has_value()) {
        sink(components.behaviour->behaviourAssetId, std::string_view{ "behaviour" }, nullptr);
    }
    if (components.audioSource.has_value()) {
        sink(components.audioSource->clipAssetId, std::string_view{ "audioClip" }, nullptr);
    }
    if (components.animator.has_value()) {
        sink(components.animator->controllerAssetId, std::string_view{ "animatorController" }, nullptr);
    }
    if (components.skeletonBinding.has_value()) {
        sink(components.skeletonBinding->skeletonAssetId, std::string_view{ "skeleton" }, nullptr);
    }
    if (components.deformedGeometry.has_value()) {
        sink(components.deformedGeometry->skeletalMeshAssetId, std::string_view{ "skeletalMesh" }, nullptr);
        for (std::uint32_t slot = 0U;
             slot < components.deformedGeometry->materialSlotOverrideCount &&
             slot < kMaxDeformedGeometryMaterialSlotOverrides;
             ++slot) {
            sink(components.deformedGeometry->materialSlotAssetIds[slot], std::string_view{ "skeletalMaterialSlot" }, nullptr);
        }
    }
}

} // namespace kb::scene::SceneComponentAssetReferences
