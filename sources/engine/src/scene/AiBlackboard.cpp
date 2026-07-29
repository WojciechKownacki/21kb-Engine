#include "engine/scene/AiBlackboard.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace kb::scene {
namespace {

constexpr std::array<std::uint8_t, 8U> kMagic{ '2', '1', 'K', 'B', 'A', 'I', 'B', 0U };
constexpr std::uint32_t kVersion = 1U;
constexpr std::uint32_t kMaxEntries = 65'536U;
constexpr std::size_t kMaxStringBytes = 1U << 20U;

void U8(std::vector<std::uint8_t>& bytes, std::uint8_t value) { bytes.push_back(value); }
void U32(std::vector<std::uint8_t>& bytes, std::uint32_t value) { for (unsigned shift = 0U; shift != 32U; shift += 8U) U8(bytes, static_cast<std::uint8_t>(value >> shift)); }
void U64(std::vector<std::uint8_t>& bytes, std::uint64_t value) { for (unsigned shift = 0U; shift != 64U; shift += 8U) U8(bytes, static_cast<std::uint8_t>(value >> shift)); }
void String(std::vector<std::uint8_t>& bytes, std::string_view value) { U32(bytes, static_cast<std::uint32_t>(value.size())); bytes.insert(bytes.end(), value.begin(), value.end()); }
struct Reader {
    std::span<const std::uint8_t> bytes; std::size_t cursor = 0U;
    bool Raw(void* out, std::size_t size) noexcept { if (size > bytes.size() - cursor) return false; std::memcpy(out, bytes.data() + cursor, size); cursor += size; return true; }
    bool U8(std::uint8_t& value) noexcept { return Raw(&value, 1U); }
    bool U32(std::uint32_t& value) noexcept { std::uint8_t raw[4]{}; if (!Raw(raw, 4U)) return false; value = std::uint32_t{raw[0]} | (std::uint32_t{raw[1]} << 8U) | (std::uint32_t{raw[2]} << 16U) | (std::uint32_t{raw[3]} << 24U); return true; }
    bool U64(std::uint64_t& value) noexcept { std::uint8_t raw[8]{}; if (!Raw(raw, 8U)) return false; value = 0U; for (unsigned index = 0U; index != 8U; ++index) value |= std::uint64_t{raw[index]} << (index * 8U); return true; }
    bool String(std::string& value, std::size_t maximum) { std::uint32_t size = 0U; if (!U32(size) || size > maximum || size > bytes.size() - cursor) return false; value.assign(reinterpret_cast<const char*>(bytes.data() + cursor), size); cursor += size; return true; }
};
[[nodiscard]] bool ValidValue(const kb::save::SaveValue& value) noexcept {
    return value.type <= kb::save::SaveValueType::AssetRef && (value.type != kb::save::SaveValueType::Float || std::isfinite(value.floatValue)) && value.stringValue.size() <= kMaxStringBytes;
}
void WriteValue(std::vector<std::uint8_t>& bytes, const kb::save::SaveValue& value) {
    U8(bytes, static_cast<std::uint8_t>(value.type));
    switch (value.type) { case kb::save::SaveValueType::Bool: U8(bytes, value.boolValue ? 1U : 0U); break; case kb::save::SaveValueType::Int: U64(bytes, static_cast<std::uint64_t>(value.intValue)); break; case kb::save::SaveValueType::Float: { std::uint64_t raw = 0U; std::memcpy(&raw, &value.floatValue, sizeof(raw)); U64(bytes, raw); break; } case kb::save::SaveValueType::String: String(bytes, value.stringValue); break; case kb::save::SaveValueType::AssetRef: U64(bytes, value.assetIdValue); break; }
}
[[nodiscard]] bool ReadValue(Reader& reader, kb::save::SaveValue& value) {
    std::uint8_t type = 0U; if (!reader.U8(type) || type > static_cast<std::uint8_t>(kb::save::SaveValueType::AssetRef)) return false; value.type = static_cast<kb::save::SaveValueType>(type);
    switch (value.type) { case kb::save::SaveValueType::Bool: { std::uint8_t raw = 0U; if (!reader.U8(raw) || raw > 1U) return false; value.boolValue = raw != 0U; return true; } case kb::save::SaveValueType::Int: { std::uint64_t raw = 0U; if (!reader.U64(raw)) return false; value.intValue = static_cast<std::int64_t>(raw); return true; } case kb::save::SaveValueType::Float: { std::uint64_t raw = 0U; if (!reader.U64(raw)) return false; std::memcpy(&value.floatValue, &raw, sizeof(raw)); return std::isfinite(value.floatValue); } case kb::save::SaveValueType::String: return reader.String(value.stringValue, kMaxStringBytes); case kb::save::SaveValueType::AssetRef: return reader.U64(value.assetIdValue); } return false;
}
}

AiBlackboard::Values* AiBlackboard::FindMutable(AiBlackboardScope scope, std::uint64_t owner) noexcept { if (scope == AiBlackboardScope::World) return &world_; if (owner == 0U) return nullptr; return scope == AiBlackboardScope::Team ? &teams_[owner] : &entities_[owner]; }
const AiBlackboard::Values* AiBlackboard::Find(AiBlackboardScope scope, std::uint64_t owner) const noexcept { if (scope == AiBlackboardScope::World) return &world_; const auto& scopes = scope == AiBlackboardScope::Team ? teams_ : entities_; const auto it = scopes.find(owner); return it == scopes.end() ? nullptr : &it->second; }
bool AiBlackboard::Set(AiBlackboardScope scope, std::uint64_t owner, const AiBlackboardKey& key, kb::save::SaveValue value) { Values* values = FindMutable(scope, owner); if (values == nullptr || !key.IsValid() || value.type != key.type || !ValidValue(value)) return false; (*values)[key.name] = std::move(value); return true; }
std::optional<kb::save::SaveValue> AiBlackboard::Get(AiBlackboardScope scope, std::uint64_t owner, const AiBlackboardKey& key) const { const Values* values = Find(scope, owner); if (values == nullptr || !key.IsValid()) return std::nullopt; const auto it = values->find(key.name); return it == values->end() || it->second.type != key.type ? std::nullopt : std::optional<kb::save::SaveValue>{it->second}; }
bool AiBlackboard::Remove(AiBlackboardScope scope, std::uint64_t owner, std::string_view key) noexcept { if (scope == AiBlackboardScope::World) return world_.erase(std::string{key}) != 0U; auto& scopes = scope == AiBlackboardScope::Team ? teams_ : entities_; const auto scopeIt = scopes.find(owner); return scopeIt != scopes.end() && scopeIt->second.erase(std::string{key}) != 0U; }
void AiBlackboard::Clear(AiBlackboardScope scope, std::uint64_t owner) noexcept { if (scope == AiBlackboardScope::World) world_.clear(); else if (scope == AiBlackboardScope::Team) teams_.erase(owner); else entities_.erase(owner); }

std::vector<std::uint8_t> AiBlackboard::Serialize() const { std::vector<std::uint8_t> out(kMagic.begin(), kMagic.end()); U32(out, kVersion); std::vector<std::pair<std::pair<AiBlackboardScope,std::uint64_t>, const Values*>> scopes{{{AiBlackboardScope::World,0U},&world_}}; for (const auto& [owner, values] : teams_) scopes.push_back({{AiBlackboardScope::Team,owner},&values}); for (const auto& [owner, values] : entities_) scopes.push_back({{AiBlackboardScope::Entity,owner},&values}); std::sort(scopes.begin(),scopes.end(),[](const auto& a,const auto& b){ return a.first < b.first; }); U32(out,static_cast<std::uint32_t>(scopes.size())); for(const auto& [scope, values]:scopes){ U8(out,static_cast<std::uint8_t>(scope.first)); U64(out,scope.second); std::vector<std::string> keys; for(const auto& [key,value]:*values){static_cast<void>(value);keys.push_back(key);} std::sort(keys.begin(),keys.end()); U32(out,static_cast<std::uint32_t>(keys.size())); for(const auto& key:keys){String(out,key);WriteValue(out,values->at(key));}} return out; }
bool AiBlackboard::Deserialize(std::span<const std::uint8_t> bytes) { Reader reader{bytes}; std::array<std::uint8_t,8U> magic{}; std::uint32_t version=0U,scopeCount=0U; if(!reader.Raw(magic.data(),magic.size())||magic!=kMagic||!reader.U32(version)||version!=kVersion||!reader.U32(scopeCount)||scopeCount>kMaxEntries) return false; AiBlackboard candidate; std::vector<std::pair<AiBlackboardScope,std::uint64_t>> seen; for(std::uint32_t s=0U;s<scopeCount;++s){std::uint8_t scopeRaw=0U;std::uint64_t owner=0U;std::uint32_t count=0U;if(!reader.U8(scopeRaw)||scopeRaw>static_cast<std::uint8_t>(AiBlackboardScope::World)||!reader.U64(owner)||!reader.U32(count)||count>kMaxEntries) return false; const auto scope=static_cast<AiBlackboardScope>(scopeRaw); if((scope==AiBlackboardScope::World&&owner!=0U)||std::find(seen.begin(),seen.end(),std::pair{scope,owner})!=seen.end())return false; seen.emplace_back(scope,owner); Values* values=candidate.FindMutable(scope,owner);if(values==nullptr) return false; for(std::uint32_t i=0U;i<count;++i){std::string key;kb::save::SaveValue value;if(!reader.String(key,128U)||key.empty()||!ReadValue(reader,value)||!values->emplace(std::move(key),std::move(value)).second)return false;}} if(reader.cursor!=bytes.size())return false; *this=std::move(candidate);return true; }

} // namespace kb::scene
