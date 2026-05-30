#pragma once

#include "engine/ecs/ComponentSerialization.hpp"

namespace kb::ecs {

class SerializedComponentBuilder {
public:
    explicit SerializedComponentBuilder(const ComponentReflection& reflection);

    [[nodiscard]] bool AddField(const ComponentFieldReflection& field, ComponentFieldValue value);
    [[nodiscard]] SerializedComponent Build();

private:
    SerializedComponent component_;
};

} // namespace kb::ecs
