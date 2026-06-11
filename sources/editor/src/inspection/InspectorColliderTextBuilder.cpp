#include "inspection/InspectorColliderTextBuilder.hpp"

#include "inspection/InspectorComponentLabelFormatter.hpp"

#include <cstdio>

namespace kb::editor {

void InspectorColliderTextBuilder::Append(std::string& text, const kb::scene::ColliderComponent& collider) const {
    char component[384]{};
    std::snprintf(
        component,
        sizeof(component),
        "\n\nCollider\nShape: %s\nCenter: %.2f, %.2f, %.2f\nBox size: %.2f, %.2f, %.2f\nRadius: %.2f\nHeight: %.2f\nTrigger: %s",
        InspectorComponentLabelFormatter::ColliderShapeName(collider.shape),
        static_cast<double>(collider.center.x),
        static_cast<double>(collider.center.y),
        static_cast<double>(collider.center.z),
        static_cast<double>(collider.boxSize.x),
        static_cast<double>(collider.boxSize.y),
        static_cast<double>(collider.boxSize.z),
        static_cast<double>(collider.radius),
        static_cast<double>(collider.height),
        collider.trigger ? "true" : "false");
    text += component;
}

} // namespace kb::editor
