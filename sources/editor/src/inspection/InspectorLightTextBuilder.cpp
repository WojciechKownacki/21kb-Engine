#include "inspection/InspectorLightTextBuilder.hpp"

#include "inspection/InspectorComponentLabelFormatter.hpp"

#include <cstdio>
#include <string>

namespace kb::editor {

void InspectorLightTextBuilder::Append(std::string& text, const kb::scene::LightComponent& light) const {
    char component[384]{};
    std::snprintf(
        component,
        sizeof(component),
        "\n\nLight\nKind: %s\nIntensity: %.2f\nRange: %.2f\nColor Temperature: %s\nLayer Mask: 0x%08X",
        InspectorComponentLabelFormatter::LightKindName(light.kind),
        light.intensity,
        light.range,
        light.useColorTemperature ? (std::to_string(static_cast<int>(light.colorTemperatureKelvin)) + "K").c_str() : "Off",
        light.layerMask);
    text += component;
}

} // namespace kb::editor
