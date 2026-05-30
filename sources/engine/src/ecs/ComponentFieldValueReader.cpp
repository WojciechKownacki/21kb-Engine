#include "ecs/serialization/ComponentFieldValueReader.hpp"

#include "ecs/serialization/ComponentBytesFieldValueCodec.hpp"
#include "ecs/serialization/ComponentFloatArrayFieldValueCodec.hpp"
#include "ecs/serialization/ComponentScalarFieldValueCodec.hpp"

namespace kb::ecs {

bool ComponentFieldValueReader::Read(const void* component, std::size_t componentSize, const ComponentFieldReflection& field, ComponentFieldValue& output) noexcept {
    switch (field.type) {
    case ComponentFieldType::Bool:
        return ComponentScalarFieldValueCodec::Read<bool>(component, componentSize, field, output);
    case ComponentFieldType::Int32:
    case ComponentFieldType::Enum32:
        return ComponentScalarFieldValueCodec::Read<std::int32_t>(component, componentSize, field, output);
    case ComponentFieldType::UInt32:
        return ComponentScalarFieldValueCodec::Read<std::uint32_t>(component, componentSize, field, output);
    case ComponentFieldType::Float32:
        return ComponentScalarFieldValueCodec::Read<float>(component, componentSize, field, output);
    case ComponentFieldType::Float64:
        return ComponentScalarFieldValueCodec::Read<double>(component, componentSize, field, output);
    case ComponentFieldType::Vec2Float32:
        return ComponentFloatArrayFieldValueCodec::Read<2>(component, componentSize, field, output);
    case ComponentFieldType::Vec3Float32:
        return ComponentFloatArrayFieldValueCodec::Read<3>(component, componentSize, field, output);
    case ComponentFieldType::Vec4Float32:
        return ComponentFloatArrayFieldValueCodec::Read<4>(component, componentSize, field, output);
    case ComponentFieldType::Bytes:
        return ComponentBytesFieldValueCodec::Read(component, componentSize, field, output);
    }
    return false;
}

} // namespace kb::ecs
