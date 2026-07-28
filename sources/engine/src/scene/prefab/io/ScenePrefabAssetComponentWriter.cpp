#include "scene/prefab/io/ScenePrefabAssetComponentWriter.hpp"

#include "engine/scene/SceneTransforms.hpp"

#include <cstdint>
#include <ostream>
#include <string_view>

namespace kb::scene {
namespace {

void WriteVec3(std::ostream& output, const char* key, Vec3 value) {
    output << key << '=' << value.x << ' ' << value.y << ' ' << value.z << '\n';
}

} // namespace

void ScenePrefabAssetComponentWriter::Write(std::ostream& output, const ScenePrefabNodeComponents& components) {
    output << "camera=" << (components.camera.has_value() ? 1 : 0) << '\n';
    if (components.camera.has_value()) {
        output << "camera.projection=" << static_cast<int>(components.camera->projection) << '\n';
        output << "camera.verticalFovDegrees=" << components.camera->verticalFovDegrees << '\n';
        output << "camera.orthographicHeight=" << components.camera->orthographicHeight << '\n';
        output << "camera.nearClip=" << components.camera->nearClip << '\n';
        output << "camera.farClip=" << components.camera->farClip << '\n';
        output << "camera.primary=" << (components.camera->primary ? 1 : 0) << '\n';
        output << "camera.viewportId=" << components.camera->viewportId << '\n';
        output << "camera.priority=" << components.camera->priority << '\n';
        output << "camera.cullingMask=" << components.camera->cullingMask << '\n';
        output << "camera.clearMode=" << static_cast<int>(components.camera->clearMode) << '\n';
        WriteVec3(output, "camera.clearColor", components.camera->clearColor);
    }

    output << "meshRenderer=" << (components.meshRenderer.has_value() ? 1 : 0) << '\n';
    if (components.meshRenderer.has_value()) {
        output << "meshRenderer.meshAssetId=" << components.meshRenderer->meshAssetId << '\n';
        output << "meshRenderer.materialAssetId=" << components.meshRenderer->materialAssetId << '\n';
        output << "meshRenderer.materialSlotOverrideCount=" << components.meshRenderer->materialSlotOverrideCount << '\n';
        for (std::uint32_t slotIndex = 0U; slotIndex < components.meshRenderer->materialSlotOverrideCount && slotIndex < kMaxMeshRendererMaterialSlotOverrides; ++slotIndex) {
            output << "meshRenderer.materialSlotAssetId." << slotIndex << '=' << components.meshRenderer->materialSlotAssetIds[slotIndex] << '\n';
        }
        output << "meshRenderer.castsShadow=" << (components.meshRenderer->castsShadow ? 1 : 0) << '\n';
        output << "meshRenderer.receivesShadow=" << (components.meshRenderer->receivesShadow ? 1 : 0) << '\n';
        output << "meshRenderer.layer=" << components.meshRenderer->layer << '\n';
    }

    output << "light=" << (components.light.has_value() ? 1 : 0) << '\n';
    if (components.light.has_value()) {
        output << "light.kind=" << static_cast<int>(components.light->kind) << '\n';
        WriteVec3(output, "light.color", components.light->color);
        output << "light.intensity=" << components.light->intensity << '\n';
        output << "light.range=" << components.light->range << '\n';
        output << "light.innerConeDegrees=" << components.light->innerConeDegrees << '\n';
        output << "light.outerConeDegrees=" << components.light->outerConeDegrees << '\n';
        output << "light.areaWidth=" << components.light->areaWidth << '\n';
        output << "light.areaHeight=" << components.light->areaHeight << '\n';
        output << "light.contactShadowLength=" << components.light->contactShadowLength << '\n';
        output << "light.volumetricScattering=" << components.light->volumetricScattering << '\n';
        output << "light.castsShadow=" << (components.light->castsShadow ? 1 : 0) << '\n';
        output << "light.useColorTemperature=" << (components.light->useColorTemperature ? 1 : 0) << '\n';
        output << "light.colorTemperatureKelvin=" << components.light->colorTemperatureKelvin << '\n';
        output << "light.layerMask=" << components.light->layerMask << '\n';
    }

    output << "input=" << (components.input.has_value() ? 1 : 0) << '\n';
    if (components.input.has_value()) {
        output << "input.mappingContextAssetId=" << components.input->mappingContextAssetId << '\n';
        output << "input.priority=" << components.input->priority << '\n';
        output << "input.enabled=" << (components.input->enabled ? 1 : 0) << '\n';
        output << "input.localUser=" << components.input->localUser.value << '\n';
    }

    output << "rigidbody=" << (components.rigidbody.has_value() ? 1 : 0) << '\n';
    if (components.rigidbody.has_value()) {
        output << "rigidbody.bodyType=" << static_cast<int>(components.rigidbody->bodyType) << '\n';
        output << "rigidbody.mass=" << components.rigidbody->mass << '\n';
        WriteVec3(output, "rigidbody.linearVelocity", components.rigidbody->linearVelocity);
        WriteVec3(output, "rigidbody.angularVelocity", components.rigidbody->angularVelocity);
        output << "rigidbody.gravityScale=" << components.rigidbody->gravityScale << '\n';
        output << "rigidbody.useGravity=" << (components.rigidbody->useGravity ? 1 : 0) << '\n';
        output << "rigidbody.lockRotation=" << (components.rigidbody->lockRotation ? 1 : 0) << '\n';
    }

    output << "collider=" << (components.collider.has_value() ? 1 : 0) << '\n';
    if (components.collider.has_value()) {
        output << "collider.shape=" << static_cast<int>(components.collider->shape) << '\n';
        WriteVec3(output, "collider.center", components.collider->center);
        WriteVec3(output, "collider.boxSize", components.collider->boxSize);
        output << "collider.radius=" << components.collider->radius << '\n';
        output << "collider.height=" << components.collider->height << '\n';
        output << "collider.trigger=" << (components.collider->trigger ? 1 : 0) << '\n';
        output << "collider.friction=" << components.collider->friction << '\n';
        output << "collider.restitution=" << components.collider->restitution << '\n';
        output << "collider.layer=" << components.collider->layer << '\n';
    }

    output << "characterController=" << (components.characterController.has_value() ? 1 : 0) << '\n';
    if (components.characterController.has_value()) {
        WriteVec3(output, "characterController.center", components.characterController->center);
        output << "characterController.radius=" << components.characterController->radius << '\n';
        output << "characterController.height=" << components.characterController->height << '\n';
        output << "characterController.slopeLimitDegrees=" << components.characterController->slopeLimitDegrees << '\n';
        output << "characterController.stepOffset=" << components.characterController->stepOffset << '\n';
        output << "characterController.gravityScale=" << components.characterController->gravityScale << '\n';
        output << "characterController.useGravity=" << (components.characterController->useGravity ? 1 : 0) << '\n';
    }

    output << "joint=" << (components.joint.has_value() ? 1 : 0) << '\n';
    if (components.joint.has_value()) {
        output << "joint.type=" << static_cast<int>(components.joint->type) << '\n';
        output << "joint.connectedNodeStableId=" << components.joint->connectedNodeStableId << '\n';
        WriteVec3(output, "joint.anchor", components.joint->anchor);
        WriteVec3(output, "joint.connectedAnchor", components.joint->connectedAnchor);
        WriteVec3(output, "joint.axis", components.joint->axis);
        output << "joint.minLimit=" << components.joint->minLimit << '\n';
        output << "joint.maxLimit=" << components.joint->maxLimit << '\n';
        output << "joint.enableLimit=" << (components.joint->enableLimit ? 1 : 0) << '\n';
    }

    output << "tags=" << (components.tags.has_value() ? 1 : 0) << '\n';
    if (components.tags.has_value()) {
        output << "tags.value=" << TagsText(*components.tags) << '\n';
    }

    output << "behaviour=" << (components.behaviour.has_value() ? 1 : 0) << '\n';
    if (components.behaviour.has_value()) {
        output << "behaviour.behaviourAssetId=" << components.behaviour->behaviourAssetId << '\n';
        output << "behaviour.backend=" << static_cast<int>(components.behaviour->backend) << '\n';
        output << "behaviour.enabled=" << (components.behaviour->enabled ? 1 : 0) << '\n';
        output << "behaviour.tickGroup=" << static_cast<int>(components.behaviour->tickGroup) << '\n';
        output << "behaviour.executionOrder=" << components.behaviour->executionOrder << '\n';
    }

    output << "audioSource=" << (components.audioSource.has_value() ? 1 : 0) << '\n';
    if (components.audioSource.has_value()) {
        output << "audioSource.clipAssetId=" << components.audioSource->clipAssetId << '\n';
        output << "audioSource.volume=" << components.audioSource->volume << '\n';
        output << "audioSource.pitch=" << components.audioSource->pitch << '\n';
        output << "audioSource.loop=" << (components.audioSource->loop ? 1 : 0) << '\n';
        output << "audioSource.spatial=" << (components.audioSource->spatial ? 1 : 0) << '\n';
        output << "audioSource.autoplay=" << (components.audioSource->autoplay ? 1 : 0) << '\n';
        output << "audioSource.enabled=" << (components.audioSource->enabled ? 1 : 0) << '\n';
        output << "audioSource.mute=" << (components.audioSource->mute ? 1 : 0) << '\n';
        output << "audioSource.pan=" << components.audioSource->pan << '\n';
        output << "audioSource.spatialBlend=" << components.audioSource->spatialBlend << '\n';
        output << "audioSource.attenuationModel=" << static_cast<int>(components.audioSource->attenuationModel) << '\n';
        output << "audioSource.minDistance=" << components.audioSource->minDistance << '\n';
        output << "audioSource.maxDistance=" << components.audioSource->maxDistance << '\n';
        output << "audioSource.rolloff=" << components.audioSource->rolloff << '\n';
        output << "audioSource.dopplerFactor=" << components.audioSource->dopplerFactor << '\n';
        // LIB-147: written only when routed off the implicit master, so pre-LIB-147
        // prefab files stay byte-identical on a pure re-save.
        if (!AudioSourceOutputBus(*components.audioSource).empty()) {
            output << "audioSource.outputBus=" << AudioSourceOutputBus(*components.audioSource) << '\n';
        }
    }

    output << "audioListener=" << (components.audioListener.has_value() ? 1 : 0) << '\n';
    if (components.audioListener.has_value()) {
        output << "audioListener.primary=" << (components.audioListener->primary ? 1 : 0) << '\n';
        output << "audioListener.enabled=" << (components.audioListener->enabled ? 1 : 0) << '\n';
    }
    output << "animator=" << (components.animator.has_value() ? 1 : 0) << '\n';
    if (components.animator.has_value()) {
        output << "animator.controllerAssetId=" << components.animator->controllerAssetId << '\n';
        output << "animator.speed=" << components.animator->speed << '\n';
        output << "animator.enabled=" << (components.animator->enabled ? 1 : 0) << '\n';
        output << "animator.rootMotionOwner=" << static_cast<int>(components.animator->rootMotionOwner) << '\n';
    }
}

} // namespace kb::scene
