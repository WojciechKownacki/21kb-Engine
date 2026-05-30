#pragma once

#include "engine/ecs/ComponentSerialization.hpp"

#include <string_view>

namespace kb::ecs {

class SerializedComponentFieldFinder {
public:
    [[nodiscard]] static const SerializedComponentField* Find(const SerializedComponent& component, std::string_view fieldName) noexcept;
};

} // namespace kb::ecs
