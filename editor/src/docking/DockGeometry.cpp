#include "docking/DockGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {
namespace {

constexpr float kMinRatio = 0.05F;
constexpr float kMaxRatio = 0.95F;

} // namespace

int DockGeometry::ClampInt(int value, int minValue, int maxValue) {
    return std::max(minValue, std::min(value, maxValue));
}

float DockGeometry::ClampRatio(float value) {
    return std::max(kMinRatio, std::min(value, kMaxRatio));
}

DockRect DockGeometry::MakeRect(int x, int y, int width, int height) {
    return DockRect{ .x = x, .y = y, .width = std::max(0, width), .height = std::max(0, height) };
}

DockRect DockGeometry::Split(const DockRect& rect, DockDropZone zone, float ratio) {
    switch (zone) {
    case DockDropZone::Left:
        return MakeRect(rect.x, rect.y, static_cast<int>(std::round(static_cast<float>(rect.width) * ratio)), rect.height);
    case DockDropZone::Right: {
        const int width = static_cast<int>(std::round(static_cast<float>(rect.width) * ratio));
        return MakeRect(rect.x + rect.width - width, rect.y, width, rect.height);
    }
    case DockDropZone::Top:
        return MakeRect(rect.x, rect.y, rect.width, static_cast<int>(std::round(static_cast<float>(rect.height) * ratio)));
    case DockDropZone::Bottom: {
        const int height = static_cast<int>(std::round(static_cast<float>(rect.height) * ratio));
        return MakeRect(rect.x, rect.y + rect.height - height, rect.width, height);
    }
    case DockDropZone::Center:
    case DockDropZone::None:
    default:
        return rect;
    }
}

} // namespace kb::editor
