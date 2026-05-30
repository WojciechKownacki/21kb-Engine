#include "ecs/reflection/ComponentFieldBounds.hpp"

namespace kb::ecs {

bool ComponentFieldBounds::Contains(std::size_t componentSize, std::size_t fieldOffset, std::size_t fieldSize) noexcept {
    return componentSize != 0 && fieldSize != 0 && fieldOffset < componentSize && fieldSize <= componentSize - fieldOffset;
}

} // namespace kb::ecs
