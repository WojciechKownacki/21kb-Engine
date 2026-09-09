#pragma once

#include "engine/scene/SceneSystemQueryAccess.hpp"
#include "engine/scene/SceneSystemTransformAccess.hpp"

#include <string>

namespace kb::ecs {

class World;

} // namespace kb::ecs

namespace kb::scene {

class Scene;

class SceneSystemContext {
public:
    SceneSystemContext(Scene& scene, float deltaSeconds) noexcept;

    [[nodiscard]] Scene& GetScene() noexcept;
    [[nodiscard]] const Scene& GetScene() const noexcept;
    [[nodiscard]] kb::ecs::World& EcsWorld() noexcept;
    [[nodiscard]] const kb::ecs::World& EcsWorld() const noexcept;
    [[nodiscard]] float DeltaSeconds() const noexcept;

    [[nodiscard]] SceneSystemTransformAccess& Transforms() noexcept;
    [[nodiscard]] const SceneSystemTransformAccess& Transforms() const noexcept;
    [[nodiscard]] SceneSystemQueryAccess& Queries() noexcept;
    [[nodiscard]] const SceneSystemQueryAccess& Queries() const noexcept;

    // Reports a failure the system detected and handled itself, into the same de-duplicated
    // list a thrown exception lands in. Use it instead of discarding a false return: a
    // refusal nobody can see is indistinguishable from working code.
    void ReportError(std::string message);

private:
    Scene& scene_;
    SceneSystemTransformAccess transforms_;
    SceneSystemQueryAccess queries_;
    float deltaSeconds_ = 0.0F;
};

} // namespace kb::scene
