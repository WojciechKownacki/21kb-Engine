#pragma once

#include "engine/ecs/ComponentReflection.hpp"

#include <cstddef>

#define KB_ECS_FIELD(ComponentType, FieldName, FieldTypeValue) \
    ::kb::ecs::ComponentFieldDesc{ #FieldName, FieldTypeValue, offsetof(ComponentType, FieldName), sizeof(((ComponentType*)0)->FieldName) }
