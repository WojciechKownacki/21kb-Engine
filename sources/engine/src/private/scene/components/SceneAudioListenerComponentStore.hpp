#pragma once

#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneAudioListenerComponentStore {
public:
    SceneAudioListenerComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const AudioListenerComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] AudioListenerComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const AudioListenerComponent& audioListener);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene
