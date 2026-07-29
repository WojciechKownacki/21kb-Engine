#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kb::network {

enum class ReplicatedFieldType : std::uint8_t { Boolean, UnsignedInteger, QuantizedFloat };

struct ReplicatedField {
    std::uint16_t id = 0U;
    std::string name;
    ReplicatedFieldType type = ReplicatedFieldType::Boolean;
    float minimum = 0.0F;
    float maximum = 0.0F;
    std::uint8_t quantizationBits = 0U;
};

struct ReplicationSchema {
    std::uint16_t version = 0U;
    std::vector<ReplicatedField> fields;
};

[[nodiscard]] bool ValidateReplicationSchema(const ReplicationSchema& schema) noexcept;
[[nodiscard]] std::optional<std::uint32_t> QuantizeFloat(const ReplicatedField& field, float value) noexcept;
[[nodiscard]] std::optional<float> DequantizeFloat(const ReplicatedField& field, std::uint32_t value) noexcept;
[[nodiscard]] std::vector<std::uint16_t> ComputeDeltaFields(const ReplicationSchema& schema, const std::vector<std::uint64_t>& baseline, const std::vector<std::uint64_t>& current);

} // namespace kb::network
