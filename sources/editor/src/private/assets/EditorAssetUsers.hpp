#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace kb::editor {

// For each asset id, the names of the open scene's objects that use it
// (kb::scene::CollectSceneAssetUsers over the scene as it is now).
using EditorAssetUsers = std::unordered_map<std::uint64_t, std::vector<std::string>>;

} // namespace kb::editor
