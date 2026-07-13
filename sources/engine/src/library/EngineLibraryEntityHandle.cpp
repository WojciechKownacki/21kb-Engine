#include "engine/library/EngineLibraryEntityHandle.hpp"

#include "engine/scene/SceneEntities.hpp"

#include <stdexcept>
#include <string>

namespace kb::library {

namespace {

// Shared by Validate() and CheckError(): which of the three checks fails
// first, and the message naming it. std::nullopt means the handle is
// valid. Kept as plain data (not throwing/optional-building itself) so
// both callers format the failure their own way without duplicating the
// three checks.
[[nodiscard]] const char* FirstFailureReason(const EntityHandle& handle, const kb::scene::Scene& scene) {
    if (!handle.IsValid()) {
        return "received an invalid entity handle";
    }
    if (handle.SceneId() != scene.Id()) {
        return "received a handle that belongs to a different world";
    }
    if (!scene.Entities().IsAlive(handle.Entity())) {
        return "received a stale entity handle";
    }
    return nullptr;
}

} // namespace

bool EntityHandle::IsAlive(const kb::scene::Scene& scene) const noexcept {
    if (!entity_.IsValid() || sceneId_ != scene.Id()) {
        return false;
    }
    return scene.Entities().IsAlive(entity_);
}

void EntityHandle::Validate(const kb::scene::Scene& scene, std::string_view operation) const {
    const char* reason = FirstFailureReason(*this, scene);
    if (reason == nullptr) {
        return;
    }
    const std::string message = "kb::library EntityHandle " + std::string{ operation } + " " + reason;
    if (!entity_.IsValid()) {
        throw std::invalid_argument(message);
    }
    throw std::out_of_range(message);
}

std::optional<ScriptError> EntityHandle::CheckError(const kb::scene::Scene& scene, std::string_view operation) const {
    const char* reason = FirstFailureReason(*this, scene);
    if (reason == nullptr) {
        return std::nullopt;
    }
    return ScriptError{
        .code = LibraryErrorCode::InvalidHandle,
        .operation = std::string{ operation },
        .message = reason,
    };
}

} // namespace kb::library
