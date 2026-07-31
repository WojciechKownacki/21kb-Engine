#include "scene/prefab/ScenePrefabBakedData.hpp"

#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace kb::scene {
namespace {

[[nodiscard]] std::uint32_t ComponentMask(const ScenePrefabNodeComponents& components) noexcept {
    std::uint32_t mask = 0U;
    if (components.camera.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Camera);
    }
    if (components.meshRenderer.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::MeshRenderer);
    }
    if (components.light.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Light);
    }
    if (components.input.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Input);
    }
    if (components.rigidbody.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Rigidbody);
    }
    if (components.collider.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Collider);
    }
    if (components.characterController.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::CharacterController);
    }
    if (components.joint.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Joint);
    }
    if (components.tags.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Tags);
    }
    if (components.regionShape.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::RegionShape);
    }
    if (components.guideCurve.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::GuideCurve);
    }
    if (components.contentInstance.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::ContentInstance);
    }
    if (components.streamFocus.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::StreamFocus);
    }
    if (components.worldBackdrop.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::WorldBackdrop);
    }
    if (components.ambientRadiance.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::AmbientRadiance);
    }
    if (components.detailSwitch.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::DetailSwitch);
    }
    if (components.visibilityBlocker.has_value()) mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::VisibilityBlocker);
    if (components.visibilityCell.has_value()) mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::VisibilityCell);
    if (components.regionPortal.has_value()) mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::RegionPortal);
    if (components.auxFrame.has_value()) mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::AuxFrame);
    if (components.geometrySwarm.has_value()) mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::GeometrySwarm);
    if (components.behaviour.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Behaviour);
    }
    if (components.audioSource.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::AudioSource);
    }
    if (components.audioListener.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::AudioListener);
    }
    if (components.animator.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Animator);
    }
    if (components.uiDocument.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::UIDocument);
    }
    if (components.navAgent.has_value()) mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::NavAgent);
    if (components.navObstacle.has_value()) mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::NavObstacle);
    return mask;
}

} // namespace

ScenePrefabBakedData ScenePrefabBakedData::Bake(std::span<const ScenePrefabNodeDesc> nodes) {
    if (nodes.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Scene prefab baked node count exceeds addressable node indices");
    }

    ScenePrefabBakedData data;
    data.nodeCount_ = nodes.size();
    data.archetypes_.reserve(nodes.size());

    // A sparse expected-O(1) index avoids allocating one entry for every possible
    // component mask. Prefab baking is data-dependent: only masks present in the
    // authored nodes consume memory, which keeps this off the runtime hot path.
    std::unordered_map<std::uint32_t, std::size_t> archetypeByMask;
    archetypeByMask.reserve(nodes.size());

    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        const ScenePrefabNodeDesc& node = nodes[nodeIndex];
        const std::uint32_t mask = ComponentMask(node.components);
        const auto found = archetypeByMask.find(mask);
        std::size_t archetypeIndex = 0U;
        if (found == archetypeByMask.end()) {
            archetypeIndex = data.archetypes_.size();
            archetypeByMask.emplace(mask, archetypeIndex);
            data.archetypes_.push_back(ScenePrefabBakedArchetype{ .componentMask = mask });
        } else {
            archetypeIndex = found->second;
        }

        ScenePrefabBakedArchetype& archetype = data.archetypes_[archetypeIndex];
        TransformComponent transform = node.transform;
        transform.worldDirty = true;
        archetype.nodeIndices.push_back(static_cast<std::uint32_t>(nodeIndex));
        archetype.transforms.push_back(transform);
        archetype.visibility.push_back(node.visibility);
        if (node.components.camera.has_value()) {
            archetype.cameras.push_back(*node.components.camera);
        }
        if (node.components.meshRenderer.has_value()) {
            archetype.meshRenderers.push_back(*node.components.meshRenderer);
        }
        if (node.components.light.has_value()) {
            archetype.lights.push_back(*node.components.light);
        }
        if (node.components.input.has_value()) {
            archetype.inputs.push_back(*node.components.input);
        }
        if (node.components.rigidbody.has_value()) {
            archetype.rigidbodies.push_back(*node.components.rigidbody);
        }
        if (node.components.collider.has_value()) {
            archetype.colliders.push_back(*node.components.collider);
        }
        if (node.components.characterController.has_value()) {
            archetype.characterControllers.push_back(*node.components.characterController);
        }
        if (node.components.joint.has_value()) {
            const ScenePrefabJointComponent& prefabJoint = *node.components.joint;
            archetype.joints.push_back(JointComponent{
                .type = prefabJoint.type,
                .connectedEntity = {},
                .anchor = prefabJoint.anchor,
                .connectedAnchor = prefabJoint.connectedAnchor,
                .axis = prefabJoint.axis,
                .minLimit = prefabJoint.minLimit,
                .maxLimit = prefabJoint.maxLimit,
                .enableLimit = prefabJoint.enableLimit,
            });
        }
        if (node.components.tags.has_value()) {
            archetype.tags.push_back(*node.components.tags);
        }
        if (node.components.regionShape.has_value()) {
            archetype.regionShapes.push_back(*node.components.regionShape);
        }
        if (node.components.guideCurve.has_value()) {
            archetype.guideCurves.push_back(*node.components.guideCurve);
        }
        if (node.components.contentInstance.has_value()) {
            archetype.contentInstances.push_back(*node.components.contentInstance);
        }
        if (node.components.streamFocus.has_value()) {
            archetype.streamFocuses.push_back(*node.components.streamFocus);
        }
        if (node.components.worldBackdrop.has_value()) {
            archetype.worldBackdrops.push_back(*node.components.worldBackdrop);
        }
        if (node.components.ambientRadiance.has_value()) {
            archetype.ambientRadiances.push_back(*node.components.ambientRadiance);
        }
        if (node.components.detailSwitch.has_value()) {
            archetype.detailSwitches.push_back(*node.components.detailSwitch);
        }
        if (node.components.visibilityBlocker.has_value()) archetype.visibilityBlockers.push_back(*node.components.visibilityBlocker);
        if (node.components.visibilityCell.has_value()) archetype.visibilityCells.push_back(*node.components.visibilityCell);
        if (node.components.regionPortal.has_value()) {
            const ScenePrefabRegionPortalComponent& prefabPortal = *node.components.regionPortal;
            archetype.regionPortals.push_back(SceneRegionPortalComponent{ .purposes = prefabPortal.purposes, .enabled = prefabPortal.enabled });
        }
        if (node.components.auxFrame.has_value()) archetype.auxFrames.push_back(*node.components.auxFrame);
        if (node.components.geometrySwarm.has_value()) archetype.geometrySwarms.push_back(*node.components.geometrySwarm);
        if (node.components.behaviour.has_value()) {
            archetype.behaviours.push_back(*node.components.behaviour);
        }
        if (node.components.audioSource.has_value()) {
            archetype.audioSources.push_back(*node.components.audioSource);
        }
        if (node.components.audioListener.has_value()) {
            archetype.audioListeners.push_back(*node.components.audioListener);
        }
        if (node.components.animator.has_value()) {
            archetype.animators.push_back(*node.components.animator);
        }
        if (node.components.uiDocument.has_value()) {
            archetype.uiDocuments.push_back(*node.components.uiDocument);
        }
        if (node.components.navAgent.has_value()) archetype.navAgents.push_back(*node.components.navAgent);
        if (node.components.navObstacle.has_value()) archetype.navObstacles.push_back(*node.components.navObstacle);
    }

    return data;
}

std::size_t ScenePrefabBakedData::NodeCount() const noexcept {
    return nodeCount_;
}

std::span<const ScenePrefabBakedArchetype> ScenePrefabBakedData::Archetypes() const noexcept {
    return archetypes_;
}

} // namespace kb::scene
