#include "scene/prefab/io/ScenePrefabAssetComponentWriter.hpp"

#include "engine/scene/SceneTransforms.hpp"

#include <cstdint>
#include <ostream>
#include <stdexcept>
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

    output << "regionShape=" << (components.regionShape.has_value() ? 1 : 0) << '\n';
    if (components.regionShape.has_value()) {
        output << "regionShape.kind=" << static_cast<int>(components.regionShape->kind) << '\n';
        WriteVec3(output, "regionShape.center", components.regionShape->center);
        WriteVec3(output, "regionShape.size", components.regionShape->size);
        output << "regionShape.radius=" << components.regionShape->radius << '\n';
        output << "regionShape.height=" << components.regionShape->height << '\n';
        output << "regionShape.enabled=" << (components.regionShape->enabled ? 1 : 0) << '\n';
    }

    output << "guideCurve=" << (components.guideCurve.has_value() ? 1 : 0) << '\n';
    if (components.guideCurve.has_value()) {
        const GuideCurveComponent& guideCurve = *components.guideCurve;
        output << "guideCurve.controlPointCount=" << guideCurve.controlPointCount << '\n';
        output << "guideCurve.interpolation=" << static_cast<int>(guideCurve.interpolation) << '\n';
        output << "guideCurve.closed=" << (guideCurve.closed ? 1 : 0) << '\n';
        output << "guideCurve.enabled=" << (guideCurve.enabled ? 1 : 0) << '\n';
        for (std::uint32_t index = 0U; index < guideCurve.controlPointCount; ++index) {
            const std::string key = "guideCurve.point" + std::to_string(index);
            WriteVec3(output, key.c_str(), guideCurve.controlPoints[index]);
        }
    }

    output << "contentInstance=" << (components.contentInstance.has_value() ? 1 : 0) << '\n';
    if (components.contentInstance.has_value()) {
        const ContentInstanceComponent& content = *components.contentInstance;
        output << "contentInstance.assetId=" << content.assetId << '\n';
        output << "contentInstance.kind=" << static_cast<int>(content.kind) << '\n';
        output << "contentInstance.lifetime=" << static_cast<int>(content.lifetime) << '\n';
        output << "contentInstance.active=" << (content.active ? 1 : 0) << '\n';
    }
    output << "streamFocus=" << (components.streamFocus.has_value() ? 1 : 0) << '\n';
    if (components.streamFocus.has_value()) {
        const StreamFocusComponent& focus = *components.streamFocus;
        output << "streamFocus.innerRadius=" << focus.innerRadius << '\n';
        output << "streamFocus.outerRadius=" << focus.outerRadius << '\n';
        output << "streamFocus.priority=" << focus.priority << '\n';
        output << "streamFocus.loadMask=" << static_cast<std::uint32_t>(focus.loadMask) << '\n';
        output << "streamFocus.enabled=" << (focus.enabled ? 1 : 0) << '\n';
    }
    output << "worldBackdrop=" << (components.worldBackdrop.has_value() ? 1 : 0) << '\n';
    if (components.worldBackdrop.has_value()) {
        const WorldBackdropComponent& backdrop = *components.worldBackdrop;
        output << "worldBackdrop.mode=" << static_cast<std::uint32_t>(backdrop.mode) << '\n';
        WriteVec3(output, "worldBackdrop.color", backdrop.color);
        WriteVec3(output, "worldBackdrop.horizonColor", backdrop.horizonColor);
        WriteVec3(output, "worldBackdrop.zenithColor", backdrop.zenithColor);
        output << "worldBackdrop.environmentAssetId=" << backdrop.environmentAssetId << '\n';
        output << "worldBackdrop.horizonHeight=" << backdrop.horizonHeight << '\n';
        output << "worldBackdrop.gradientExponent=" << backdrop.gradientExponent << '\n';
        output << "worldBackdrop.priority=" << backdrop.priority << '\n';
        output << "worldBackdrop.enabled=" << (backdrop.enabled ? 1 : 0) << '\n';
    }
    output << "ambientRadiance=" << (components.ambientRadiance.has_value() ? 1 : 0) << '\n';
    if (components.ambientRadiance.has_value()) {
        const AmbientRadianceComponent& ambient = *components.ambientRadiance;
        output << "ambientRadiance.mode=" << static_cast<std::uint32_t>(ambient.mode) << '\n';
        WriteVec3(output, "ambientRadiance.color", ambient.color);
        WriteVec3(output, "ambientRadiance.horizonColor", ambient.horizonColor);
        WriteVec3(output, "ambientRadiance.zenithColor", ambient.zenithColor);
        output << "ambientRadiance.environmentAssetId=" << ambient.environmentAssetId << '\n';
        output << "ambientRadiance.intensity=" << ambient.intensity << '\n';
        output << "ambientRadiance.diffuseIntensity=" << ambient.diffuseIntensity << '\n';
        output << "ambientRadiance.specularIntensity=" << ambient.specularIntensity << '\n';
        output << "ambientRadiance.priority=" << ambient.priority << '\n';
        output << "ambientRadiance.enabled=" << (ambient.enabled ? 1 : 0) << '\n';
    }
    output << "detailSwitch=" << (components.detailSwitch.has_value() ? 1 : 0) << '\n';
    if (components.detailSwitch.has_value()) {
        const SceneDetailSwitchComponent& detail = *components.detailSwitch;
        output << "detailSwitch.groupId=" << detail.groupId << '\n';
        output << "detailSwitch.minimumLod=" << detail.minimumLod << '\n';
        output << "detailSwitch.maximumLod=" << detail.maximumLod << '\n';
        output << "detailSwitch.promoteCoverage=" << detail.promoteCoverage << '\n';
        output << "detailSwitch.demoteCoverage=" << detail.demoteCoverage << '\n';
        output << "detailSwitch.enabled=" << (detail.enabled ? 1 : 0) << '\n';
    }
    output << "visibilityBlocker=" << (components.visibilityBlocker.has_value() ? 1 : 0) << '\n';
    if (components.visibilityBlocker.has_value()) {
        const SceneVisibilityBlockerComponent& blocker = *components.visibilityBlocker;
        WriteVec3(output, "visibilityBlocker.localCenter", blocker.localCenter);
        WriteVec3(output, "visibilityBlocker.size", blocker.size);
        output << "visibilityBlocker.enabled=" << (blocker.enabled ? 1 : 0) << '\n';
    }
    output << "visibilityCell=" << (components.visibilityCell.has_value() ? 1 : 0) << '\n';
    if (components.visibilityCell.has_value()) {
        const VisibilityCellComponent& cell = *components.visibilityCell;
        output << "visibilityCell.membershipMask=" << cell.membershipMask << '\n';
        output << "visibilityCell.membership=" << static_cast<int>(cell.membership) << '\n';
        output << "visibilityCell.visibilityOverride=" << static_cast<int>(cell.visibilityOverride) << '\n';
        output << "visibilityCell.enabled=" << (cell.enabled ? 1 : 0) << '\n';
    }
    output << "regionPortal=" << (components.regionPortal.has_value() ? 1 : 0) << '\n';
    if (components.regionPortal.has_value()) {
        const ScenePrefabRegionPortalComponent& portal = *components.regionPortal;
        output << "regionPortal.sourceCellNodeStableId=" << portal.sourceCellNodeStableId << '\n';
        output << "regionPortal.targetCellNodeStableId=" << portal.targetCellNodeStableId << '\n';
        output << "regionPortal.purposes=" << portal.purposes << '\n';
        output << "regionPortal.enabled=" << (portal.enabled ? 1 : 0) << '\n';
    }
    output << "auxFrame=" << (components.auxFrame.has_value() ? 1 : 0) << '\n';
    if (components.auxFrame.has_value()) {
        const AuxFrameComponent& frame = *components.auxFrame;
        output << "auxFrame.mode=" << static_cast<int>(frame.mode) << '\n';
        output << "auxFrame.imageTargetId=" << frame.imageTargetId << '\n';
        output << "auxFrame.width=" << frame.width << '\n';
        output << "auxFrame.height=" << frame.height << '\n';
        WriteVec3(output, "auxFrame.mirrorPlaneNormal", frame.mirrorPlaneNormal);
        output << "auxFrame.mirrorPlaneOffset=" << frame.mirrorPlaneOffset << '\n';
        output << "auxFrame.enabled=" << (frame.enabled ? 1 : 0) << '\n';
    }
    output << "geometrySwarm=" << (components.geometrySwarm.has_value() ? 1 : 0) << '\n';
    if (components.geometrySwarm.has_value()) {
        const GeometrySwarmComponent& swarm = *components.geometrySwarm;
        output << "geometrySwarm.meshAssetId=" << swarm.meshAssetId << '\n';
        output << "geometrySwarm.materialAssetId=" << swarm.materialAssetId << '\n';
        output << "geometrySwarm.instanceCount=" << swarm.instanceCount << '\n';
        output << "geometrySwarm.columns=" << swarm.columns << '\n';
        output << "geometrySwarm.rows=" << swarm.rows << '\n';
        output << "geometrySwarm.layers=" << swarm.layers << '\n';
        WriteVec3(output, "geometrySwarm.spacing", swarm.spacing);
        output << "geometrySwarm.instanceScale=" << swarm.instanceScale << '\n';
        output << "geometrySwarm.layer=" << swarm.layer << '\n';
        output << "geometrySwarm.castsShadow=" << (swarm.castsShadow ? 1 : 0) << '\n';
        output << "geometrySwarm.receivesShadow=" << (swarm.receivesShadow ? 1 : 0) << '\n';
        output << "geometrySwarm.enabled=" << (swarm.enabled ? 1 : 0) << '\n';
    }
    output << "surfaceCast=" << (components.surfaceCast.has_value() ? 1 : 0) << '\n';
    if (components.surfaceCast.has_value()) {
        const SurfaceCastComponent& surfaceCast = *components.surfaceCast;
        output << "surfaceCast.materialAssetId=" << surfaceCast.materialAssetId << '\n';
        output << "surfaceCast.receiverLayerMask=" << surfaceCast.receiverLayerMask << '\n';
        output << "surfaceCast.order=" << surfaceCast.order << '\n';
        output << "surfaceCast.content=" << static_cast<std::uint32_t>(surfaceCast.content) << '\n';
        output << "surfaceCast.enabled=" << (surfaceCast.enabled ? 1 : 0) << '\n';
    }
    output << "facingPanel=" << (components.facingPanel.has_value() ? 1 : 0) << '\n';
    if (components.facingPanel.has_value()) {
        const FacingPanelComponent& panel = *components.facingPanel;
        output << "facingPanel.mode=" << static_cast<std::uint32_t>(panel.mode) << '\n';
        WriteVec3(output, "facingPanel.targetPoint", panel.targetPoint);
        WriteVec3(output, "facingPanel.axis", panel.axis);
        WriteVec3(output, "facingPanel.up", panel.up);
        output << "facingPanel.enabled=" << (panel.enabled ? 1 : 0) << '\n';
    }
    output << "spaceStroke=" << (components.spaceStroke.has_value() ? 1 : 0) << '\n';
    if (components.spaceStroke.has_value()) {
        const SpaceStrokeComponent& stroke = *components.spaceStroke;
        output << "spaceStroke.meshAssetId=" << stroke.meshAssetId << '\n';
        output << "spaceStroke.materialAssetId=" << stroke.materialAssetId << '\n';
        output << "spaceStroke.mode=" << static_cast<std::uint32_t>(stroke.mode) << '\n';
        output << "spaceStroke.width=" << stroke.width << '\n';
        output << "spaceStroke.cableSag=" << stroke.cableSag << '\n';
        output << "spaceStroke.splineSegments=" << static_cast<std::uint32_t>(stroke.splineSegments) << '\n';
        output << "spaceStroke.layer=" << stroke.layer << '\n';
        output << "spaceStroke.castsShadow=" << (stroke.castsShadow ? 1 : 0) << '\n';
        output << "spaceStroke.receivesShadow=" << (stroke.receivesShadow ? 1 : 0) << '\n';
        output << "spaceStroke.enabled=" << (stroke.enabled ? 1 : 0) << '\n';
    }
    output << "historyRibbon=" << (components.historyRibbon.has_value() ? 1 : 0) << '\n';
    if (components.historyRibbon.has_value()) {
        const HistoryRibbonComponent& ribbon = *components.historyRibbon;
        output << "historyRibbon.meshAssetId=" << ribbon.meshAssetId << '\n';
        output << "historyRibbon.materialAssetId=" << ribbon.materialAssetId << '\n';
        output << "historyRibbon.lifetimeSeconds=" << ribbon.lifetimeSeconds << '\n';
        output << "historyRibbon.width=" << ribbon.width << '\n';
        output << "historyRibbon.sampleIntervalSeconds=" << ribbon.sampleIntervalSeconds << '\n';
        output << "historyRibbon.layer=" << ribbon.layer << '\n';
        output << "historyRibbon.castsShadow=" << (ribbon.castsShadow ? 1 : 0) << '\n';
        output << "historyRibbon.receivesShadow=" << (ribbon.receivesShadow ? 1 : 0) << '\n';
        output << "historyRibbon.enabled=" << (ribbon.enabled ? 1 : 0) << '\n';
    }
    output << "lensEcho=" << (components.lensEcho.has_value() ? 1 : 0) << '\n';
    if (components.lensEcho.has_value()) {
        const ScenePrefabLensEchoComponent& echo = *components.lensEcho;
        output << "lensEcho.sourceNodeStableId=" << echo.sourceNodeStableId << '\n';
        output << "lensEcho.profileMaterialAssetId=" << echo.profileMaterialAssetId << '\n';
        output << "lensEcho.intensity=" << echo.intensity << '\n';
        output << "lensEcho.size=" << echo.size << '\n';
        output << "lensEcho.layer=" << echo.layer << '\n';
        output << "lensEcho.occlusionRule=" << static_cast<std::uint32_t>(echo.occlusionRule) << '\n';
        output << "lensEcho.enabled=" << (echo.enabled ? 1 : 0) << '\n';
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
    output << "skeletonBinding=" << (components.skeletonBinding.has_value() ? 1 : 0) << '\n';
    if (components.skeletonBinding.has_value()) {
        output << "skeletonBinding.skeletonAssetId=" << components.skeletonBinding->skeletonAssetId << '\n';
        output << "skeletonBinding.compatibilitySignature=" << components.skeletonBinding->skeletonCompatibilitySignature << '\n';
        output << "skeletonBinding.enabled=" << (components.skeletonBinding->enabled ? 1 : 0) << '\n';
    }
    output << "deformedGeometry=" << (components.deformedGeometry.has_value() ? 1 : 0) << '\n';
    if (components.deformedGeometry.has_value()) {
        const DrawD3DeformedGeometryComponent& geometry = *components.deformedGeometry;
        if (geometry.poseSource.IsValid()) throw std::invalid_argument("Deformed Geometry prefab serialization requires a stable pose-source node reference");
        output << "deformedGeometry.skeletalMeshAssetId=" << geometry.skeletalMeshAssetId << '\n';
        output << "deformedGeometry.materialSlotOverrideCount=" << geometry.materialSlotOverrideCount << '\n';
        for (std::uint32_t slot = 0U; slot < kMaxDeformedGeometryMaterialSlotOverrides; ++slot) {
            output << "deformedGeometry.materialSlotAssetId." << slot << '=' << geometry.materialSlotAssetIds[slot] << '\n';
        }
        output << "deformedGeometry.lodBias=" << geometry.lodBias << '\n';
        output << "deformedGeometry.lodEnabled=" << (geometry.lodEnabled ? 1 : 0) << '\n';
        output << "deformedGeometry.fixedBounds=" << (geometry.fixedBounds ? 1 : 0) << '\n';
        output << "deformedGeometry.castsShadow=" << (geometry.castsShadow ? 1 : 0) << '\n';
        output << "deformedGeometry.receivesShadow=" << (geometry.receivesShadow ? 1 : 0) << '\n';
        output << "deformedGeometry.layer=" << geometry.layer << '\n';
        output << "deformedGeometry.enabled=" << (geometry.enabled ? 1 : 0) << '\n';
    }
    output << "uiDocument=" << (components.uiDocument.has_value() ? 1 : 0) << '\n';
    if (components.uiDocument.has_value()) {
        output << "uiDocument.documentAssetId=" << components.uiDocument->documentAssetId << '\n';
        output << "uiDocument.enabled=" << (components.uiDocument->enabled ? 1 : 0) << '\n';
    }
}

} // namespace kb::scene
