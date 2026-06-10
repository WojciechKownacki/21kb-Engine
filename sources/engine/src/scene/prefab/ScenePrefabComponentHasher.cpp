#include "scene/prefab/ScenePrefabComponentHasher.hpp"

#include "scene/prefab/ScenePrefabHashBuilder.hpp"

#include <cstdint>

namespace kb::scene {

void ScenePrefabComponentHasher::Mix(std::uint64_t& hash, const ScenePrefabNodeComponents& components) noexcept {
    ScenePrefabHashBuilder::Mix(hash, components.camera.has_value() ? 1U : 0U);
    if (components.camera.has_value()) {
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(components.camera->projection));
        ScenePrefabHashBuilder::MixFloat(hash, components.camera->verticalFovDegrees);
        ScenePrefabHashBuilder::MixFloat(hash, components.camera->orthographicHeight);
        ScenePrefabHashBuilder::MixFloat(hash, components.camera->nearClip);
        ScenePrefabHashBuilder::MixFloat(hash, components.camera->farClip);
        ScenePrefabHashBuilder::Mix(hash, components.camera->primary ? 1U : 0U);
    }

    ScenePrefabHashBuilder::Mix(hash, components.meshRenderer.has_value() ? 1U : 0U);
    if (components.meshRenderer.has_value()) {
        ScenePrefabHashBuilder::Mix(hash, components.meshRenderer->meshAssetId);
        ScenePrefabHashBuilder::Mix(hash, components.meshRenderer->materialAssetId);
        ScenePrefabHashBuilder::Mix(hash, components.meshRenderer->materialSlotOverrideCount);
        for (std::uint32_t slotIndex = 0U; slotIndex < components.meshRenderer->materialSlotOverrideCount && slotIndex < kMaxMeshRendererMaterialSlotOverrides; ++slotIndex) {
            ScenePrefabHashBuilder::Mix(hash, components.meshRenderer->materialSlotAssetIds[slotIndex]);
        }
        ScenePrefabHashBuilder::Mix(hash, components.meshRenderer->castsShadow ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, components.meshRenderer->receivesShadow ? 1U : 0U);
    }

    ScenePrefabHashBuilder::Mix(hash, components.light.has_value() ? 1U : 0U);
    if (components.light.has_value()) {
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(components.light->kind));
        ScenePrefabHashBuilder::MixVec3(hash, components.light->color);
        ScenePrefabHashBuilder::MixFloat(hash, components.light->intensity);
        ScenePrefabHashBuilder::MixFloat(hash, components.light->range);
        ScenePrefabHashBuilder::MixFloat(hash, components.light->innerConeDegrees);
        ScenePrefabHashBuilder::MixFloat(hash, components.light->outerConeDegrees);
        ScenePrefabHashBuilder::MixFloat(hash, components.light->areaWidth);
        ScenePrefabHashBuilder::MixFloat(hash, components.light->areaHeight);
        ScenePrefabHashBuilder::MixFloat(hash, components.light->contactShadowLength);
        ScenePrefabHashBuilder::MixFloat(hash, components.light->volumetricScattering);
        ScenePrefabHashBuilder::Mix(hash, components.light->castsShadow ? 1U : 0U);
    }

    ScenePrefabHashBuilder::Mix(hash, components.input.has_value() ? 1U : 0U);
    if (components.input.has_value()) {
        ScenePrefabHashBuilder::Mix(hash, components.input->mappingContextAssetId);
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(components.input->priority)));
        ScenePrefabHashBuilder::Mix(hash, components.input->enabled ? 1U : 0U);
    }
}

} // namespace kb::scene
