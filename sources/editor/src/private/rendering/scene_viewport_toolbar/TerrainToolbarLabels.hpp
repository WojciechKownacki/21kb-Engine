#pragma once

#include "TerrainBrush.hpp"

#include <string>
#include <string_view>

namespace kb::editor {

// The words of the terrain bar and its menus for brush operations and tips.
class TerrainToolbarLabels final {
public:
    TerrainToolbarLabels() = delete;

    [[nodiscard]] static std::string_view BrushLabel(kb::terrain_editor::TerrainBrushMode mode);
    [[nodiscard]] static std::string_view BrushDescription(kb::terrain_editor::TerrainBrushMode mode);
    [[nodiscard]] static std::string_view BrushToolbarLabel(kb::terrain_editor::TerrainBrushMode mode);
    [[nodiscard]] static std::string_view BrushShapeLabel(kb::terrain_editor::TerrainBrushShape shape);
    [[nodiscard]] static std::string_view BrushShapeDescription(kb::terrain_editor::TerrainBrushShape shape);
    // A value of the bar in two significant digits: 8, 0.65, 1.5e+02.
    [[nodiscard]] static std::string FormatNumber(float value);
};

} // namespace kb::editor
