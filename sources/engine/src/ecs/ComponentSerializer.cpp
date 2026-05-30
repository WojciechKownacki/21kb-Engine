#include "engine/ecs/ComponentSerialization.hpp"

#include "ecs/serialization/ComponentFieldValueReader.hpp"
#include "ecs/serialization/ComponentFieldValueWriter.hpp"
#include "ecs/serialization/SerializedComponentBuilder.hpp"
#include "ecs/serialization/SerializedComponentFieldFinder.hpp"

#include <utility>

namespace kb::ecs {

bool ComponentSerializer::Serialize(const void* component, const ComponentReflection& reflection, SerializedComponent& output) {
    if (component == nullptr || !reflection.IsValid()) {
        return false;
    }

    SerializedComponentBuilder builder{ reflection };
    for (const ComponentFieldReflection& field : reflection.Fields()) {
        ComponentFieldValue value;
        if (!ComponentFieldValueReader::Read(component, reflection.Size(), field, value) || !builder.AddField(field, std::move(value))) {
            return false;
        }
    }

    output = builder.Build();
    return true;
}

bool ComponentSerializer::Apply(const SerializedComponent& source, const ComponentReflection& reflection, void* component) {
    if (component == nullptr || !reflection.IsValid()) {
        return false;
    }

    if (!source.componentName.empty() && source.componentName != reflection.Name()) {
        return false;
    }
    if (source.componentName.empty() && source.componentId != reflection.Id()) {
        return false;
    }

    for (const ComponentFieldReflection& field : reflection.Fields()) {
        const SerializedComponentField* sourceField = SerializedComponentFieldFinder::Find(source, field.name);
        if (sourceField == nullptr || sourceField->type != field.type || !ComponentFieldValueWriter::Write(component, reflection.Size(), field, sourceField->value)) {
            return false;
        }
    }
    return true;
}

} // namespace kb::ecs
