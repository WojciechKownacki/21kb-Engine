#pragma once

#include <cstdint>

namespace kb::ecs {

class World;

} // namespace kb::ecs

namespace kb::scene {

class SceneComponentRegistry {
public:
    explicit SceneComponentRegistry(kb::ecs::World& world);

    [[nodiscard]] std::uint64_t TransformComponentId() const noexcept;
    [[nodiscard]] std::uint64_t VisibilityComponentId() const noexcept;
    [[nodiscard]] std::uint64_t BehaviourComponentId() const noexcept;
    [[nodiscard]] std::uint64_t CameraComponentId() const noexcept;
    [[nodiscard]] std::uint64_t MeshRendererComponentId() const noexcept;
    [[nodiscard]] std::uint64_t LightComponentId() const noexcept;
    [[nodiscard]] std::uint64_t InputComponentId() const noexcept;
    [[nodiscard]] std::uint64_t RigidbodyComponentId() const noexcept;
    [[nodiscard]] std::uint64_t ColliderComponentId() const noexcept;
    [[nodiscard]] std::uint64_t CharacterControllerComponentId() const noexcept;
    [[nodiscard]] std::uint64_t JointComponentId() const noexcept;
    [[nodiscard]] std::uint64_t TagsComponentId() const noexcept;
    [[nodiscard]] std::uint64_t RegionShapeComponentId() const noexcept;
    [[nodiscard]] std::uint64_t GuideCurveComponentId() const noexcept;
    [[nodiscard]] std::uint64_t ContentInstanceComponentId() const noexcept;
    [[nodiscard]] std::uint64_t StreamFocusComponentId() const noexcept;
    [[nodiscard]] std::uint64_t WorldBackdropComponentId() const noexcept;
    [[nodiscard]] std::uint64_t AmbientRadianceComponentId() const noexcept;
    [[nodiscard]] std::uint64_t DetailSwitchComponentId() const noexcept;
    [[nodiscard]] std::uint64_t VisibilityBlockerComponentId() const noexcept;
    [[nodiscard]] std::uint64_t AudioSourceComponentId() const noexcept;
    [[nodiscard]] std::uint64_t AudioListenerComponentId() const noexcept;
    [[nodiscard]] std::uint64_t AnimatorComponentId() const noexcept;
    [[nodiscard]] std::uint64_t UIDocumentComponentId() const noexcept;
    [[nodiscard]] std::uint64_t NavAgentComponentId() const noexcept;
    [[nodiscard]] std::uint64_t NavObstacleComponentId() const noexcept;

private:
    std::uint64_t transformComponentId_ = 0;
    std::uint64_t visibilityComponentId_ = 0;
    std::uint64_t behaviourComponentId_ = 0;
    std::uint64_t cameraComponentId_ = 0;
    std::uint64_t meshRendererComponentId_ = 0;
    std::uint64_t lightComponentId_ = 0;
    std::uint64_t inputComponentId_ = 0;
    std::uint64_t rigidbodyComponentId_ = 0;
    std::uint64_t colliderComponentId_ = 0;
    std::uint64_t characterControllerComponentId_ = 0;
    std::uint64_t jointComponentId_ = 0;
    std::uint64_t tagsComponentId_ = 0;
    std::uint64_t regionShapeComponentId_ = 0;
    std::uint64_t guideCurveComponentId_ = 0;
    std::uint64_t contentInstanceComponentId_ = 0;
    std::uint64_t streamFocusComponentId_ = 0;
    std::uint64_t worldBackdropComponentId_ = 0;
    std::uint64_t ambientRadianceComponentId_ = 0;
    std::uint64_t detailSwitchComponentId_ = 0;
    std::uint64_t visibilityBlockerComponentId_ = 0;
    std::uint64_t audioSourceComponentId_ = 0;
    std::uint64_t audioListenerComponentId_ = 0;
    std::uint64_t animatorComponentId_ = 0;
    std::uint64_t uiDocumentComponentId_ = 0;
    std::uint64_t navAgentComponentId_ = 0;
    std::uint64_t navObstacleComponentId_ = 0;
};

} // namespace kb::scene
