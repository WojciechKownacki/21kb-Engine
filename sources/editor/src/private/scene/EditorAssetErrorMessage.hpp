#pragma once

#include "engine/assets/AssetManager.hpp"

#include <string>

namespace kb::editor {

// Console message for a failed asset operation: the manager's last error when it
// has one, otherwise the caller's fallback text. Used by EditorSceneContext.cpp
// and EditorSceneContextSkeletalMesh.cpp; it is a pure function over the manager,
// so it carries no state of its own.
[[nodiscard]] inline std::string AssetErrorOr(const kb::assets::AssetManager& manager, const char* fallback) {
    const std::string error = manager.LastError();
    return error.empty() ? std::string{ fallback } : error;
}

} // namespace kb::editor
