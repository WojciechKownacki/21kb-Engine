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

    ScenePrefabHashBuilder::Mix(hash, components.rigidbody.has_value() ? 1U : 0U);
    if (components.rigidbody.has_value()) {
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(components.rigidbody->bodyType));
        ScenePrefabHashBuilder::MixFloat(hash, components.rigidbody->mass);
        ScenePrefabHashBuilder::MixVec3(hash, components.rigidbody->linearVelocity);
        ScenePrefabHashBuilder::MixVec3(hash, components.rigidbody->angularVelocity);
        ScenePrefabHashBuilder::MixFloat(hash, components.rigidbody->gravityScale);
        ScenePrefabHashBuilder::Mix(hash, components.rigidbody->useGravity ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, components.rigidbody->lockRotation ? 1U : 0U);
    }

    ScenePrefabHashBuilder::Mix(hash, components.collider.has_value() ? 1U : 0U);
    if (components.collider.has_value()) {
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(components.collider->shape));
        ScenePrefabHashBuilder::MixVec3(hash, components.collider->center);
        ScenePrefabHashBuilder::MixVec3(hash, components.collider->boxSize);
        ScenePrefabHashBuilder::MixFloat(hash, components.collider->radius);
        ScenePrefabHashBuilder::MixFloat(hash, components.collider->height);
        ScenePrefabHashBuilder::Mix(hash, components.collider->trigger ? 1U : 0U);
    }

    ScenePrefabHashBuilder::Mix(hash, components.tags.has_value() ? 1U : 0U);
    if (components.tags.has_value()) {
        for (char character : TagsText(*components.tags)) {
            ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(static_cast<unsigned char>(character)));
        }
    }

    ScenePrefabHashBuilder::Mix(hash, components.behaviour.has_value() ? 1U : 0U);
    if (components.behaviour.has_value()) {
        ScenePrefabHashBuilder::Mix(hash, components.behaviour->behaviourAssetId);
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(components.behaviour->backend));
        ScenePrefabHashBuilder::Mix(hash, components.behaviour->enabled ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(components.behaviour->tickGroup));
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(components.behaviour->executionOrder)));
    }

    ScenePrefabHashBuilder::Mix(hash, components.audioSource.has_value() ? 1U : 0U);
    if (components.audioSource.has_value()) {
        ScenePrefabHashBuilder::Mix(hash, components.audioSource->clipAssetId);
        ScenePrefabHashBuilder::MixFloat(hash, components.audioSource->volume);
        ScenePrefabHashBuilder::MixFloat(hash, components.audioSource->pitch);
        ScenePrefabHashBuilder::Mix(hash, components.audioSource->loop ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, components.audioSource->spatial ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, components.audioSource->autoplay ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, components.audioSource->enabled ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, components.audioSource->mute ? 1U : 0U);
        ScenePrefabHashBuilder::MixFloat(hash, components.audioSource->pan);
        ScenePrefabHashBuilder::MixFloat(hash, components.audioSource->spatialBlend);
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(components.audioSource->attenuationModel));
        ScenePrefabHashBuilder::MixFloat(hash, components.audioSource->minDistance);
        ScenePrefabHashBuilder::MixFloat(hash, components.audioSource->maxDistance);
        ScenePrefabHashBuilder::MixFloat(hash, components.audioSource->rolloff);
        ScenePrefabHashBuilder::MixFloat(hash, components.audioSource->dopplerFactor);
    }

    ScenePrefabHashBuilder::Mix(hash, components.audioListener.has_value() ? 1U : 0U);
    if (components.audioListener.has_value()) {
        ScenePrefabHashBuilder::Mix(hash, components.audioListener->primary ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, components.audioListener->enabled ? 1U : 0U);
    }
}

} // namespace kb::scene
