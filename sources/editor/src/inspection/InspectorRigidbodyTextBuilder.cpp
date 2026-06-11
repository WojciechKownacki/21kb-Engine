#include "inspection/InspectorRigidbodyTextBuilder.hpp"

#include "inspection/InspectorComponentLabelFormatter.hpp"

#include <cstdio>

namespace kb::editor {

void InspectorRigidbodyTextBuilder::Append(std::string& text, const kb::scene::RigidbodyComponent& rigidbody) const {
    char component[384]{};
    std::snprintf(
        component,
        sizeof(component),
        "\n\nRigidbody\nBody type: %s\nMass: %.2f\nLinear velocity: %.2f, %.2f, %.2f\nAngular velocity: %.2f, %.2f, %.2f\nGravity scale: %.2f\nUse gravity: %s\nLock rotation: %s",
        InspectorComponentLabelFormatter::RigidbodyBodyTypeName(rigidbody.bodyType),
        static_cast<double>(rigidbody.mass),
        static_cast<double>(rigidbody.linearVelocity.x),
        static_cast<double>(rigidbody.linearVelocity.y),
        static_cast<double>(rigidbody.linearVelocity.z),
        static_cast<double>(rigidbody.angularVelocity.x),
        static_cast<double>(rigidbody.angularVelocity.y),
        static_cast<double>(rigidbody.angularVelocity.z),
        static_cast<double>(rigidbody.gravityScale),
        rigidbody.useGravity ? "true" : "false",
        rigidbody.lockRotation ? "true" : "false");
    text += component;
}

} // namespace kb::editor
