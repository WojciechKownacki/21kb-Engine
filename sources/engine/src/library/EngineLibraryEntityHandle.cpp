#include "engine/library/EngineLibraryEntityHandle.hpp"

#include "engine/scene/SceneEntities.hpp"

#include <stdexcept>
#include <string>

namespace kb::library {

bool EntityHandle::IsAlive(const kb::scene::Scene& scene) const noexcept {
    if (!entity_.IsValid() || sceneId_ != scene.Id()) {
        return false;
    }
    return scene.Entities().IsAlive(entity_);
}

void EntityHandle::Validate(const kb::scene::Scene& scene, std::string_view operation) const {
    if (!entity_.IsValid()) {
        throw std::invalid_argument("kb::library EntityHandle " + std::string{ operation } + " received an invalid entity handle");
    }
    if (sceneId_ != scene.Id()) {
        throw std::out_of_range("kb::library EntityHandle " + std::string{ operation } + " received a handle that belongs to a different world");
    }
    if (!scene.Entities().IsAlive(entity_)) {
        throw std::out_of_range("kb::library EntityHandle " + std::string{ operation } + " received a stale entity handle");
    }
}

} // namespace kb::library
