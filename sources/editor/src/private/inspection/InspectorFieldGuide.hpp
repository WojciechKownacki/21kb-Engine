#pragma once

#include "inspection/InspectorPanelState.hpp"

#include <string>
#include <string_view>

namespace kb::editor {

// What an Inspector row says beyond its label, for the properties that have their own id (Camera, Light, Mesh
// Renderer): hover help in plain words, the unit shown after a number and, for a number with a natural range, that
// range, which draws the value as a slider. The rows of the physics components carry the same three things in
// PhysicsField, so one hover lookup serves them all.
struct InspectorFieldHelp {
    std::string_view tooltip;
    std::string_view unit;
    bool bounded = false;
    float minimum = 0.0F;
    float maximum = 0.0F;
};

class InspectorFieldGuide {
public:
    InspectorFieldGuide() = delete;

    // Empty help for a property that has none.
    [[nodiscard]] static InspectorFieldHelp Help(InspectorPropertyId property);

    // The hover help of any row, by the same section / property / row index a hit carries. Empty when the row has none.
    [[nodiscard]] static std::string Tooltip(InspectorSectionId section, InspectorPropertyId property, int index);
};

} // namespace kb::editor
