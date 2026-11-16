#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

// One module component of a saved scene or prefab node: its registered name and its field values, encoded by name and
// type. A node keeps the bytes of a component whose module is not loaded, and writes them back unchanged.
struct SceneModuleComponentData {
    std::string name;
    std::vector<std::uint8_t> bytes;

    [[nodiscard]] bool operator==(const SceneModuleComponentData&) const noexcept = default;
};

} // namespace kb::scene
