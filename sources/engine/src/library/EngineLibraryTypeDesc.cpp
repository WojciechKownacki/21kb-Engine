#include "engine/library/EngineLibraryTypeDesc.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace kb::library {

namespace {

using kb::script::ScriptValue;
using kb::script::ScriptValueType;
using kb::script::ToString;
using kb::script::ToVisualGraphValueType;

constexpr std::size_t kTypeCount = 13U;

std::array<LibraryTypeDesc, kTypeCount> BuildDescriptors() {
    std::array<LibraryTypeDesc, kTypeCount> descriptors{};

    descriptors[static_cast<std::size_t>(ScriptValueType::Void)] = LibraryTypeDesc{
        .scriptType = ScriptValueType::Void,
        .canonicalName = ToString(ScriptValueType::Void),
        .visualGraphPinType = ToVisualGraphValueType(ScriptValueType::Void),
        .luaTypeName = "nil",
        .supportsEquality = true,
        .defaultValue = ScriptValue{},
    };
    descriptors[static_cast<std::size_t>(ScriptValueType::Bool)] = LibraryTypeDesc{
        .scriptType = ScriptValueType::Bool,
        .canonicalName = ToString(ScriptValueType::Bool),
        .visualGraphPinType = ToVisualGraphValueType(ScriptValueType::Bool),
        .luaTypeName = "boolean",
        .supportsEquality = true,
        .defaultValue = ScriptValue{ false },
    };
    descriptors[static_cast<std::size_t>(ScriptValueType::Int)] = LibraryTypeDesc{
        .scriptType = ScriptValueType::Int,
        .canonicalName = ToString(ScriptValueType::Int),
        .visualGraphPinType = ToVisualGraphValueType(ScriptValueType::Int),
        .luaTypeName = "number (integer)",
        .supportsEquality = true,
        .defaultValue = ScriptValue{ 0 },
    };
    descriptors[static_cast<std::size_t>(ScriptValueType::Float)] = LibraryTypeDesc{
        .scriptType = ScriptValueType::Float,
        .canonicalName = ToString(ScriptValueType::Float),
        .visualGraphPinType = ToVisualGraphValueType(ScriptValueType::Float),
        .luaTypeName = "number",
        .supportsEquality = true,
        .defaultValue = ScriptValue{ 0.0F },
    };
    descriptors[static_cast<std::size_t>(ScriptValueType::String)] = LibraryTypeDesc{
        .scriptType = ScriptValueType::String,
        .canonicalName = ToString(ScriptValueType::String),
        .visualGraphPinType = ToVisualGraphValueType(ScriptValueType::String),
        .luaTypeName = "string",
        .supportsEquality = true,
        .defaultValue = ScriptValue{ std::string{} },
    };
    descriptors[static_cast<std::size_t>(ScriptValueType::Entity)] = LibraryTypeDesc{
        .scriptType = ScriptValueType::Entity,
        .canonicalName = ToString(ScriptValueType::Entity),
        .visualGraphPinType = ToVisualGraphValueType(ScriptValueType::Entity),
        .luaTypeName = "number (integer, entity id)",
        .supportsEquality = true,
        .defaultValue = ScriptValue{ 0U, ScriptValueType::Entity },
    };
    descriptors[static_cast<std::size_t>(ScriptValueType::Component)] = LibraryTypeDesc{
        .scriptType = ScriptValueType::Component,
        .canonicalName = ToString(ScriptValueType::Component),
        .visualGraphPinType = ToVisualGraphValueType(ScriptValueType::Component),
        .luaTypeName = "number (integer, component id)",
        .supportsEquality = true,
        .defaultValue = ScriptValue{ 0U, ScriptValueType::Component },
    };
    descriptors[static_cast<std::size_t>(ScriptValueType::Int64)] = LibraryTypeDesc{
        .scriptType = ScriptValueType::Int64,
        .canonicalName = ToString(ScriptValueType::Int64),
        .visualGraphPinType = ToVisualGraphValueType(ScriptValueType::Int64),
        .luaTypeName = "number (integer, 64-bit)",
        .supportsEquality = true,
        .defaultValue = ScriptValue{ std::int64_t{ 0 } },
    };
    descriptors[static_cast<std::size_t>(ScriptValueType::UInt32)] = LibraryTypeDesc{
        .scriptType = ScriptValueType::UInt32,
        .canonicalName = ToString(ScriptValueType::UInt32),
        .visualGraphPinType = ToVisualGraphValueType(ScriptValueType::UInt32),
        .luaTypeName = "number (integer, unsigned 32-bit)",
        .supportsEquality = true,
        .defaultValue = ScriptValue{ std::uint32_t{ 0U } },
    };
    descriptors[static_cast<std::size_t>(ScriptValueType::Double)] = LibraryTypeDesc{
        .scriptType = ScriptValueType::Double,
        .canonicalName = ToString(ScriptValueType::Double),
        .visualGraphPinType = ToVisualGraphValueType(ScriptValueType::Double),
        .luaTypeName = "number (double precision)",
        .supportsEquality = true,
        .defaultValue = ScriptValue{ 0.0 },
    };
    descriptors[static_cast<std::size_t>(ScriptValueType::Name)] = LibraryTypeDesc{
        .scriptType = ScriptValueType::Name,
        .canonicalName = ToString(ScriptValueType::Name),
        .visualGraphPinType = ToVisualGraphValueType(ScriptValueType::Name),
        .luaTypeName = "string (identifier)",
        .supportsEquality = true,
        .defaultValue = ScriptValue{ std::string{}, ScriptValueType::Name },
    };
    descriptors[static_cast<std::size_t>(ScriptValueType::Guid)] = LibraryTypeDesc{
        .scriptType = ScriptValueType::Guid,
        .canonicalName = ToString(ScriptValueType::Guid),
        .visualGraphPinType = ToVisualGraphValueType(ScriptValueType::Guid),
        .luaTypeName = "string (GUID)",
        .supportsEquality = true,
        .defaultValue = ScriptValue{ std::string{}, ScriptValueType::Guid },
    };
    descriptors[static_cast<std::size_t>(ScriptValueType::Hash)] = LibraryTypeDesc{
        .scriptType = ScriptValueType::Hash,
        .canonicalName = ToString(ScriptValueType::Hash),
        .visualGraphPinType = ToVisualGraphValueType(ScriptValueType::Hash),
        .luaTypeName = "number (integer, opaque 64-bit hash)",
        .supportsEquality = true,
        .defaultValue = ScriptValue{ 0U, ScriptValueType::Hash },
    };

    return descriptors;
}

} // namespace

const LibraryTypeDesc& DescribeType(ScriptValueType type) noexcept {
    static const std::array<LibraryTypeDesc, kTypeCount> kDescriptors = BuildDescriptors();
    const std::size_t index = static_cast<std::size_t>(type);
    return index < kDescriptors.size() ? kDescriptors[index] : kDescriptors[static_cast<std::size_t>(ScriptValueType::Void)];
}

} // namespace kb::library
