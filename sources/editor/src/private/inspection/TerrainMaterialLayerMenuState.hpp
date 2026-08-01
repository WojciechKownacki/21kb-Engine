#pragma once

#include "inspection/InspectorPanelState.hpp"

namespace kb::editor::terrain_material_layer_menu {

// TerrainEditEnabled is a retained Inspector property token that has no rendered
// row. Reusing it here keeps the widely included InspectorPanelState contract
// untouched for a transient menu that belongs only to the Terrain Inspector.
inline constexpr InspectorPropertyId kProperty = InspectorPropertyId::TerrainEditEnabled;

[[nodiscard]] inline bool& Storage() noexcept {
    static bool open = false;
    return open;
}

inline void Toggle() noexcept { Storage() = !Storage(); }
inline void Close() noexcept { Storage() = false; }
[[nodiscard]] inline bool Open() noexcept { return Storage(); }

} // namespace kb::editor::terrain_material_layer_menu
