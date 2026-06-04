#pragma once

#include <cstddef>
#include <cstdint>

struct ecs_world_t;

namespace kb::scene {

class SceneComponentRegistry {
public:
    explicit SceneComponentRegistry(ecs_world_t& world);

    [[nodiscard]] std::uint64_t TransformComponentId() const noexcept;
    [[nodiscard]] std::uint64_t VisibilityComponentId() const noexcept;
    [[nodiscard]] std::uint64_t BehaviourComponentId() const noexcept;
    [[nodiscard]] std::uint64_t CameraComponentId() const noexcept;
    [[nodiscard]] std::uint64_t MeshRendererComponentId() const noexcept;
    [[nodiscard]] std::uint64_t LightComponentId() const noexcept;

private:
    [[nodiscard]] static std::uint64_t RegisterComponent(ecs_world_t& world, const char* name, std::size_t size, std::size_t alignment);

    std::uint64_t transformComponentId_ = 0;
    std::uint64_t visibilityComponentId_ = 0;
    std::uint64_t behaviourComponentId_ = 0;
    std::uint64_t cameraComponentId_ = 0;
    std::uint64_t meshRendererComponentId_ = 0;
    std::uint64_t lightComponentId_ = 0;
};

} // namespace kb::scene
