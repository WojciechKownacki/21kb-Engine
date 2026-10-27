#pragma once

#include <filesystem>
#include <string>

namespace kb::scene {

// Apply only to a newly copied file or directory before publishing it to asset
// discovery. Gives scenes and prefabs fresh identities and redirects references
// between copied prefabs. Stable node IDs and references outside the copy remain.
// On failure the caller must discard the entire unpublished copy.
[[nodiscard]] bool ReidentifyCopiedSceneAssets(const std::filesystem::path& copy, std::string& error);

} // namespace kb::scene
