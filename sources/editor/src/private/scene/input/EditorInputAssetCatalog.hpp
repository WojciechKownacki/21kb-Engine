#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::editor {

// Enumerates registered assets of a given runtime type and resolves cyclic
// "next" selection. Used wherever the UI cycles through available assets
// (mapping context on a component, action on a mapping).
//
// Single responsibility: read-only asset cataloguing.
class EditorInputAssetCatalog {
public:
    explicit EditorInputAssetCatalog(const kb::scene::Scene& scene) noexcept;

    [[nodiscard]] std::vector<std::uint64_t> SortedIdsOfType(std::string_view type) const;

    // Returns the id following `current` in `ids` (wrapping), or the first id
    // when `current` is absent. Returns `current` when `ids` is empty.
    [[nodiscard]] static std::uint64_t NextCyclicId(const std::vector<std::uint64_t>& ids, std::uint64_t current);

private:
    const kb::scene::Scene& scene_;
};

} // namespace kb::editor
