#include "ecs/query/QueryFieldReader.hpp"

#include <flecs.h>

namespace kb::ecs {

bool QueryFieldReader::Read(
    ecs_iter_t& iterator,
    std::span<const std::size_t> componentSizes,
    QueryComponentPointerBlock& fieldComponents) noexcept {
    bool fieldsReady = true;
    for (std::size_t field = 0; field < componentSizes.size(); ++field) {
        fieldComponents[field] = ecs_field_w_size(&iterator, static_cast<ecs_size_t>(componentSizes[field]), static_cast<int8_t>(field));
        fieldsReady = fieldsReady && fieldComponents[field] != nullptr;
    }
    return fieldsReady;
}

bool QueryFieldReader::ReadMutable(
    ecs_iter_t& iterator,
    std::span<const std::size_t> componentSizes,
    MutableQueryComponentPointerBlock& fieldComponents) noexcept {
    bool fieldsReady = true;
    for (std::size_t field = 0; field < componentSizes.size(); ++field) {
        fieldComponents[field] = ecs_field_w_size(&iterator, static_cast<ecs_size_t>(componentSizes[field]), static_cast<int8_t>(field));
        fieldsReady = fieldsReady && fieldComponents[field] != nullptr;
    }
    return fieldsReady;
}

} // namespace kb::ecs
