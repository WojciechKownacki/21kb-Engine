#include "ecs/reflection/ComponentReflectionFactory.hpp"

#include <utility>

namespace kb::ecs {

ComponentReflection ComponentReflectionFactory::Create(
    ComponentId componentId,
    std::string_view componentName,
    std::size_t componentSize,
    std::span<const ComponentFieldDesc> fields) {
    std::vector<ComponentFieldReflection> reflectedFields;
    reflectedFields.reserve(fields.size());
    for (const ComponentFieldDesc& field : fields) {
        reflectedFields.push_back(ComponentFieldReflection{
            .name = std::string{ field.name },
            .type = field.type,
            .offset = field.offset,
            .size = field.size,
        });
    }

    return ComponentReflection{ componentId, std::string{ componentName }, componentSize, std::move(reflectedFields) };
}

} // namespace kb::ecs
