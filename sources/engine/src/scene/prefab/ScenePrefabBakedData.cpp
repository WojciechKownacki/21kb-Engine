#include "scene/prefab/ScenePrefabBakedData.hpp"

#include <array>
#include <limits>
#include <stdexcept>

namespace kb::scene {
namespace {

inline constexpr std::size_t kScenePrefabBakedMaskCount = 1U << 11U;

[[nodiscard]] std::uint16_t ComponentMask(const ScenePrefabNodeComponents& components) noexcept {
    std::uint16_t mask = 0U;
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
    if (components.tags.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Tags);
    }
    if (components.behaviour.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Behaviour);
    }
    if (components.audioSource.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::AudioSource);
    }
    if (components.audioListener.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::AudioListener);
    }
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

    std::array<std::size_t, kScenePrefabBakedMaskCount> archetypeByMask{};
    archetypeByMask.fill(std::numeric_limits<std::size_t>::max());

    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        const ScenePrefabNodeDesc& node = nodes[nodeIndex];
        const std::uint16_t mask = ComponentMask(node.components);
        std::size_t archetypeIndex = archetypeByMask[mask];
        if (archetypeIndex == std::numeric_limits<std::size_t>::max()) {
            archetypeIndex = data.archetypes_.size();
            archetypeByMask[mask] = archetypeIndex;
            data.archetypes_.push_back(ScenePrefabBakedArchetype{ .componentMask = mask });
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
        if (node.components.tags.has_value()) {
            archetype.tags.push_back(*node.components.tags);
        }
        if (node.components.behaviour.has_value()) {
            archetype.behaviours.push_back(*node.components.behaviour);
        }
        if (node.components.audioSource.has_value()) {
            archetype.audioSources.push_back(*node.components.audioSource);
        }
        if (node.components.audioListener.has_value()) {
            archetype.audioListeners.push_back(*node.components.audioListener);
        }
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
