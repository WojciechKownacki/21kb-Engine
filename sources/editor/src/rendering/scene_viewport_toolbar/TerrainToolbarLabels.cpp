#include "rendering/scene_viewport_toolbar/TerrainToolbarLabels.hpp"

#include "localization/EditorLocalization.hpp"

#include <array>
#include <cstdio>

namespace kb::editor {

std::string_view TerrainToolbarLabels::BrushLabel(kb::terrain_editor::TerrainBrushMode mode) {
    using Mode = kb::terrain_editor::TerrainBrushMode;
    switch (mode) {
    case Mode::Raise: return EditorText("terrainTool.brush.raise", "Raise / Mountain");
    case Mode::Lower: return EditorText("terrainTool.brush.lower", "Lower / Valley");
    case Mode::Smooth: return EditorText("terrainTool.brush.smooth", "Smooth");
    case Mode::Flatten: return EditorText("terrainTool.brush.flatten", "Flatten");
    case Mode::Noise: return EditorText("terrainTool.brush.noise", "Noise");
    case Mode::Terrace: return EditorText("terrainTool.brush.terrace", "Terrace");
    case Mode::CutHole: return EditorText("terrainTool.brush.cutHole", "Cut Hole");
    case Mode::FillHole: return EditorText("terrainTool.brush.fillHole", "Fill Hole");
    }
    return EditorText("terrainTool.brush.raise", "Raise / Mountain");
}

std::string_view TerrainToolbarLabels::BrushDescription(kb::terrain_editor::TerrainBrushMode mode) {
    using Mode = kb::terrain_editor::TerrainBrushMode;
    switch (mode) {
    case Mode::Raise: return EditorText("terrainTool.brushHint.raise", "Build height");
    case Mode::Lower: return EditorText("terrainTool.brushHint.lower", "Carve terrain");
    case Mode::Smooth: return EditorText("terrainTool.brushHint.smooth", "Soften details");
    case Mode::Flatten: return EditorText("terrainTool.brushHint.flatten", "Level to height");
    case Mode::Noise: return EditorText("terrainTool.brushHint.noise", "Add texture");
    case Mode::Terrace: return EditorText("terrainTool.brushHint.terrace", "Create steps");
    case Mode::CutHole: return EditorText("terrainTool.brushHint.cutHole", "Remove surface");
    case Mode::FillHole: return EditorText("terrainTool.brushHint.fillHole", "Restore surface");
    }
    return {};
}

std::string_view TerrainToolbarLabels::BrushToolbarLabel(kb::terrain_editor::TerrainBrushMode mode) {
    using Mode = kb::terrain_editor::TerrainBrushMode;
    switch (mode) {
    case Mode::Raise: return EditorText("terrainTool.brushShort.raise", "Raise");
    case Mode::Lower: return EditorText("terrainTool.brushShort.lower", "Lower");
    default: return BrushLabel(mode);
    }
}

std::string_view TerrainToolbarLabels::BrushShapeLabel(kb::terrain_editor::TerrainBrushShape shape) {
    using Shape = kb::terrain_editor::TerrainBrushShape;
    switch (shape) {
    case Shape::SoftRound: return EditorText("terrainTool.shape.softRound", "Soft Round");
    case Shape::HardRound: return EditorText("terrainTool.shape.hardRound", "Hard Round");
    case Shape::LinearRound: return EditorText("terrainTool.shape.linearRound", "Linear");
    case Shape::Bell: return EditorText("terrainTool.shape.bell", "Bell");
    case Shape::Ring: return EditorText("terrainTool.shape.ring", "Ring");
    case Shape::Speckle: return EditorText("terrainTool.shape.speckle", "Speckle");
    }
    return EditorText("terrainTool.shape.softRound", "Soft Round");
}

std::string_view TerrainToolbarLabels::BrushShapeDescription(kb::terrain_editor::TerrainBrushShape shape) {
    using Shape = kb::terrain_editor::TerrainBrushShape;
    switch (shape) {
    case Shape::SoftRound: return EditorText("terrainTool.shapeHint.softRound", "Smooth feathered edge");
    case Shape::HardRound: return EditorText("terrainTool.shapeHint.hardRound", "Solid precise edge");
    case Shape::LinearRound: return EditorText("terrainTool.shapeHint.linearRound", "Even radial fade");
    case Shape::Bell: return EditorText("terrainTool.shapeHint.bell", "Natural dome");
    case Shape::Ring: return EditorText("terrainTool.shapeHint.ring", "Circular band");
    case Shape::Speckle: return EditorText("terrainTool.shapeHint.speckle", "Organic texture");
    }
    return {};
}

std::string TerrainToolbarLabels::FormatNumber(float value) {
    std::array<char, 32U> text{};
    std::snprintf(text.data(), text.size(), "%.2g", static_cast<double>(value));
    return text.data();
}

} // namespace kb::editor
