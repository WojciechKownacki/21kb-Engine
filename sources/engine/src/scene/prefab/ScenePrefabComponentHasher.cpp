#include "scene/prefab/ScenePrefabComponentHasher.hpp"

#include "scene/prefab/ScenePrefabHashBuilder.hpp"

#include <cstdint>
#include <string_view>

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
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(components.input->localUser.value));
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
        ScenePrefabHashBuilder::MixFloat(hash, components.collider->friction);
        ScenePrefabHashBuilder::MixFloat(hash, components.collider->restitution);
        ScenePrefabHashBuilder::Mix(hash, components.collider->layer);
    }

    ScenePrefabHashBuilder::Mix(hash, components.characterController.has_value() ? 1U : 0U);
    if (components.characterController.has_value()) {
        ScenePrefabHashBuilder::MixVec3(hash, components.characterController->center);
        ScenePrefabHashBuilder::MixFloat(hash, components.characterController->radius);
        ScenePrefabHashBuilder::MixFloat(hash, components.characterController->height);
        ScenePrefabHashBuilder::MixFloat(hash, components.characterController->slopeLimitDegrees);
        ScenePrefabHashBuilder::MixFloat(hash, components.characterController->stepOffset);
        ScenePrefabHashBuilder::MixFloat(hash, components.characterController->gravityScale);
        ScenePrefabHashBuilder::Mix(hash, components.characterController->useGravity ? 1U : 0U);
    }

    ScenePrefabHashBuilder::Mix(hash, components.joint.has_value() ? 1U : 0U);
    if (components.joint.has_value()) {
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(components.joint->type));
        ScenePrefabHashBuilder::Mix(hash, components.joint->connectedNodeStableId);
        ScenePrefabHashBuilder::MixVec3(hash, components.joint->anchor);
        ScenePrefabHashBuilder::MixVec3(hash, components.joint->connectedAnchor);
        ScenePrefabHashBuilder::MixVec3(hash, components.joint->axis);
        ScenePrefabHashBuilder::MixFloat(hash, components.joint->minLimit);
        ScenePrefabHashBuilder::MixFloat(hash, components.joint->maxLimit);
        ScenePrefabHashBuilder::Mix(hash, components.joint->enableLimit ? 1U : 0U);
    }

    ScenePrefabHashBuilder::Mix(hash, components.regionPortal.has_value() ? 1U : 0U);
    if (components.regionPortal.has_value()) {
        ScenePrefabHashBuilder::Mix(hash, components.regionPortal->sourceCellNodeStableId);
        ScenePrefabHashBuilder::Mix(hash, components.regionPortal->targetCellNodeStableId);
        ScenePrefabHashBuilder::Mix(hash, components.regionPortal->purposes);
        ScenePrefabHashBuilder::Mix(hash, components.regionPortal->enabled ? 1U : 0U);
    }
    ScenePrefabHashBuilder::Mix(hash, components.auxFrame.has_value() ? 1U : 0U);
    if (components.auxFrame.has_value()) {
        const AuxFrameComponent& frame = *components.auxFrame;
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(frame.mode));
        ScenePrefabHashBuilder::Mix(hash, frame.imageTargetId);
        ScenePrefabHashBuilder::Mix(hash, frame.width);
        ScenePrefabHashBuilder::Mix(hash, frame.height);
        ScenePrefabHashBuilder::MixVec3(hash, frame.mirrorPlaneNormal);
        ScenePrefabHashBuilder::MixFloat(hash, frame.mirrorPlaneOffset);
        ScenePrefabHashBuilder::Mix(hash, frame.enabled ? 1U : 0U);
    }
    ScenePrefabHashBuilder::Mix(hash, components.geometrySwarm.has_value() ? 1U : 0U);
    if (components.geometrySwarm.has_value()) {
        const GeometrySwarmComponent& swarm = *components.geometrySwarm;
        ScenePrefabHashBuilder::Mix(hash, swarm.meshAssetId); ScenePrefabHashBuilder::Mix(hash, swarm.materialAssetId);
        ScenePrefabHashBuilder::Mix(hash, swarm.instanceCount); ScenePrefabHashBuilder::Mix(hash, swarm.columns);
        ScenePrefabHashBuilder::Mix(hash, swarm.rows); ScenePrefabHashBuilder::Mix(hash, swarm.layers);
        ScenePrefabHashBuilder::MixVec3(hash, swarm.spacing); ScenePrefabHashBuilder::MixFloat(hash, swarm.instanceScale);
        ScenePrefabHashBuilder::Mix(hash, swarm.layer); ScenePrefabHashBuilder::Mix(hash, swarm.castsShadow ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, swarm.receivesShadow ? 1U : 0U); ScenePrefabHashBuilder::Mix(hash, swarm.enabled ? 1U : 0U);
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
        ScenePrefabHashBuilder::MixString(hash, AudioSourceOutputBus(*components.audioSource));
    }

    ScenePrefabHashBuilder::Mix(hash, components.audioListener.has_value() ? 1U : 0U);
    if (components.audioListener.has_value()) {
        ScenePrefabHashBuilder::Mix(hash, components.audioListener->primary ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, components.audioListener->enabled ? 1U : 0U);
    }
    ScenePrefabHashBuilder::Mix(hash, components.animator.has_value() ? 1U : 0U);
    if (components.animator.has_value()) {
        ScenePrefabHashBuilder::Mix(hash, components.animator->controllerAssetId);
        ScenePrefabHashBuilder::MixFloat(hash, components.animator->speed);
        ScenePrefabHashBuilder::Mix(hash, components.animator->enabled ? 1U : 0U);
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint64_t>(components.animator->rootMotionOwner));
    }
    ScenePrefabHashBuilder::Mix(hash, components.uiDocument.has_value() ? 1U : 0U);
    if (components.uiDocument.has_value()) {
        ScenePrefabHashBuilder::Mix(hash, components.uiDocument->documentAssetId);
        ScenePrefabHashBuilder::Mix(hash, components.uiDocument->enabled ? 1U : 0U);
    }
}

} // namespace kb::scene
