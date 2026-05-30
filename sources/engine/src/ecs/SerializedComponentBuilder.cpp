#include "ecs/serialization/SerializedComponentBuilder.hpp"

#include <utility>

namespace kb::ecs {

SerializedComponentBuilder::SerializedComponentBuilder(const ComponentReflection& reflection) {
    component_.componentId = reflection.Id();
    component_.componentName = std::string{ reflection.Name() };
    component_.fields.reserve(reflection.Fields().size());
}

bool SerializedComponentBuilder::AddField(const ComponentFieldReflection& field, ComponentFieldValue value) {
    if (field.name.empty()) {
        return false;
    }

    component_.fields.push_back(SerializedComponentField{
        .name = field.name,
        .type = field.type,
        .value = std::move(value),
    });
    return true;
}

SerializedComponent SerializedComponentBuilder::Build() {
    return std::move(component_);
}

} // namespace kb::ecs
