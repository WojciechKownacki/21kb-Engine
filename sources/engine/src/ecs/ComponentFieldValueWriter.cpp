#include "ecs/serialization/ComponentFieldValueWriter.hpp"

#include "ecs/serialization/ComponentBytesFieldValueCodec.hpp"
#include "ecs/serialization/ComponentFloatArrayFieldValueCodec.hpp"
#include "ecs/serialization/ComponentScalarFieldValueCodec.hpp"

namespace kb::ecs {

bool ComponentFieldValueWriter::Write(void* component, std::size_t componentSize, const ComponentFieldReflection& field, const ComponentFieldValue& value) noexcept {
    switch (field.type) {
    case ComponentFieldType::Bool:
        return ComponentScalarFieldValueCodec::Write<bool>(component, componentSize, field, value);
    case ComponentFieldType::Int32:
    case ComponentFieldType::Enum32:
        return ComponentScalarFieldValueCodec::Write<std::int32_t>(component, componentSize, field, value);
    case ComponentFieldType::UInt32:
        return ComponentScalarFieldValueCodec::Write<std::uint32_t>(component, componentSize, field, value);
    case ComponentFieldType::Float32:
        return ComponentScalarFieldValueCodec::Write<float>(component, componentSize, field, value);
    case ComponentFieldType::Float64:
        return ComponentScalarFieldValueCodec::Write<double>(component, componentSize, field, value);
    case ComponentFieldType::Vec2Float32:
        return ComponentFloatArrayFieldValueCodec::Write<2>(component, componentSize, field, value);
    case ComponentFieldType::Vec3Float32:
        return ComponentFloatArrayFieldValueCodec::Write<3>(component, componentSize, field, value);
    case ComponentFieldType::Vec4Float32:
        return ComponentFloatArrayFieldValueCodec::Write<4>(component, componentSize, field, value);
    case ComponentFieldType::Bytes:
        return ComponentBytesFieldValueCodec::Write(component, componentSize, field, value);
    }
    return false;
}

} // namespace kb::ecs
