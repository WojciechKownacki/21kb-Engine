#pragma once

#include <cstdint>
#include <string>

namespace kb::save {

// LIB-162: the scalar value kinds a SaveGame entry can hold. Deliberately
// flat scalars only — a save is a string -> scalar table, mirroring
// kb::script::ScriptSharedState (the script boundary carries no collection
// type; LIB-058 deferred that). Serialized as a one-byte tag, so the on-disk
// numeric values are part of the format contract — never renumber them.
enum class SaveValueType : std::uint8_t {
    Bool = 0,
    Int = 1,
    Float = 2,
    String = 3,
    AssetRef = 4,
};

// A single tagged scalar. Only the field matching `type` is meaningful; the
// others hold their zero value. Kept a plain value type (no std::variant) so
// it serializes field-by-field exactly like the rest of the engine's binary
// formats.
struct SaveValue {
    SaveValueType type = SaveValueType::Int;
    bool boolValue = false;
    std::int64_t intValue = 0;
    double floatValue = 0.0;
    std::uint64_t assetIdValue = 0U;
    std::string stringValue;

    [[nodiscard]] static SaveValue MakeBool(bool value) {
        return SaveValue{ .type = SaveValueType::Bool, .boolValue = value };
    }
    [[nodiscard]] static SaveValue MakeInt(std::int64_t value) {
        return SaveValue{ .type = SaveValueType::Int, .intValue = value };
    }
    [[nodiscard]] static SaveValue MakeFloat(double value) {
        return SaveValue{ .type = SaveValueType::Float, .floatValue = value };
    }
    [[nodiscard]] static SaveValue MakeString(std::string value) {
        return SaveValue{ .type = SaveValueType::String, .stringValue = std::move(value) };
    }
    [[nodiscard]] static SaveValue MakeAssetRef(std::uint64_t value) {
        return SaveValue{ .type = SaveValueType::AssetRef, .assetIdValue = value };
    }
};

} // namespace kb::save
