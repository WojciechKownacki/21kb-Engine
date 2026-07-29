#include "engine/network/ReplicationSchema.hpp"

#include <cmath>
#include <limits>

namespace kb::network {
namespace {
[[nodiscard]] bool IsValidField(const ReplicatedField& field) noexcept { if(field.id==0U||field.name.empty())return false; if(field.type!=ReplicatedFieldType::QuantizedFloat)return field.quantizationBits==0U; return std::isfinite(field.minimum)&&std::isfinite(field.maximum)&&field.minimum<field.maximum&&field.quantizationBits>0U&&field.quantizationBits<=24U; }
} // namespace
bool ValidateReplicationSchema(const ReplicationSchema& schema) noexcept { if(schema.version==0U||schema.fields.empty())return false; for(std::size_t index=0;index<schema.fields.size();++index){if(!IsValidField(schema.fields[index]))return false; for(std::size_t previous=0;previous<index;++previous)if(schema.fields[previous].id==schema.fields[index].id)return false;} return true; }
std::optional<std::uint32_t> QuantizeFloat(const ReplicatedField& field, float value) noexcept { if(field.type!=ReplicatedFieldType::QuantizedFloat||!IsValidField(field)||!std::isfinite(value)||value<field.minimum||value>field.maximum)return std::nullopt; const std::uint32_t levels=(1U<<field.quantizationBits)-1U; return static_cast<std::uint32_t>(std::lround((value-field.minimum)/(field.maximum-field.minimum)*static_cast<float>(levels))); }
std::optional<float> DequantizeFloat(const ReplicatedField& field, std::uint32_t value) noexcept { if(field.type!=ReplicatedFieldType::QuantizedFloat||!IsValidField(field))return std::nullopt; const std::uint32_t levels=(1U<<field.quantizationBits)-1U; if(value>levels)return std::nullopt; return field.minimum+(field.maximum-field.minimum)*(static_cast<float>(value)/static_cast<float>(levels)); }
std::vector<std::uint16_t> ComputeDeltaFields(const ReplicationSchema& schema, const std::vector<std::uint64_t>& baseline, const std::vector<std::uint64_t>& current) { std::vector<std::uint16_t> changed; if(!ValidateReplicationSchema(schema)||baseline.size()!=schema.fields.size()||current.size()!=schema.fields.size())return changed; for(std::size_t index=0;index<current.size();++index)if(baseline[index]!=current[index])changed.push_back(schema.fields[index].id); return changed; }
} // namespace kb::network
