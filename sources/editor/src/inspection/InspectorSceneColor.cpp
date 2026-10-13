#include "inspection/InspectorSceneColor.hpp"
#include "localization/EditorLocalization.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace kb::editor {

bool InspectorSceneColor::IsColorProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::LightColor:
    case InspectorPropertyId::CameraClearColor:
    case InspectorPropertyId::WorldBackdropColor:
    case InspectorPropertyId::WorldBackdropHorizonColor:
    case InspectorPropertyId::WorldBackdropZenithColor:
    case InspectorPropertyId::WorldBackdropSkyTint:
    case InspectorPropertyId::WorldBackdropGroundColor:
    case InspectorPropertyId::AmbientRadianceColor:
    case InspectorPropertyId::AmbientRadianceHorizonColor:
    case InspectorPropertyId::AmbientRadianceZenithColor:
        return true;
    default:
        return false;
    }
}

std::string_view InspectorSceneColor::Label(InspectorPropertyId property) {
    switch (property) {
    case InspectorPropertyId::LightColor:
    case InspectorPropertyId::WorldBackdropColor:
    case InspectorPropertyId::AmbientRadianceColor: return EditorText("inspector.color.color", "Color");
    case InspectorPropertyId::CameraClearColor: return EditorText("inspector.color.clearColor", "Clear Color");
    case InspectorPropertyId::WorldBackdropHorizonColor:
    case InspectorPropertyId::AmbientRadianceHorizonColor: return EditorText("inspector.color.horizon", "Horizon");
    case InspectorPropertyId::WorldBackdropZenithColor:
    case InspectorPropertyId::AmbientRadianceZenithColor: return EditorText("inspector.color.zenith", "Zenith");
    case InspectorPropertyId::WorldBackdropSkyTint: return EditorText("inspector.color.skyTint", "Sky Tint");
    case InspectorPropertyId::WorldBackdropGroundColor: return EditorText("inspector.color.ground", "Ground");
    default: return {};
    }
}

float InspectorSceneColor::LinearToSrgb(float channel) noexcept {
    channel = std::clamp(channel, 0.0F, 1.0F);
    return channel <= 0.0031308F ? channel * 12.92F : 1.055F * std::pow(channel, 1.0F / 2.4F) - 0.055F;
}

float InspectorSceneColor::SrgbToLinear(float channel) noexcept {
    channel = std::clamp(channel, 0.0F, 1.0F);
    return channel <= 0.04045F ? channel / 12.92F : std::pow((channel + 0.055F) / 1.055F, 2.4F);
}

} // namespace kb::editor
