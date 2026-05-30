#include "ecs/serialization/SerializedComponentFieldFinder.hpp"

namespace kb::ecs {

const SerializedComponentField* SerializedComponentFieldFinder::Find(const SerializedComponent& component, std::string_view fieldName) noexcept {
    for (const SerializedComponentField& field : component.fields) {
        if (field.name == fieldName) {
            return &field;
        }
    }
    return nullptr;
}

} // namespace kb::ecs
