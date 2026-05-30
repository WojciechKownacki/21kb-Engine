#include "inspection/InspectorLightTextBuilder.hpp"

#include "inspection/InspectorComponentLabelFormatter.hpp"

#include <cstdio>

namespace kb::editor {

void InspectorLightTextBuilder::Append(std::string& text, const kb::scene::LightComponent& light) const {
    char component[256]{};
    std::snprintf(
        component,
        sizeof(component),
        "\n\nLight\nKind: %s\nIntensity: %.2f\nRange: %.2f",
        InspectorComponentLabelFormatter::LightKindName(light.kind),
        light.intensity,
        light.range);
    text += component;
}

} // namespace kb::editor
