#include "engine/save/SaveGame.hpp"

#include "engine/save/SaveDomain.hpp"

#include <utility>

namespace kb::save {

std::string_view ToString(SaveDomain domain) noexcept {
    switch (domain) {
    case SaveDomain::SaveGame:
        return "SaveGame";
    case SaveDomain::UserSettings:
        return "UserSettings";
    }
    return "SaveGame";
}

namespace {

[[nodiscard]] const SaveValue* FindTyped(const std::unordered_map<std::string, SaveValue>& entries, std::string_view key, SaveValueType type) {
    const auto iterator = entries.find(std::string{ key });
    if (iterator == entries.end() || iterator->second.type != type) {
        return nullptr;
    }
    return &iterator->second;
}

} // namespace

void SaveGame::SetBool(std::string key, bool value) {
    entries_[std::move(key)] = SaveValue::MakeBool(value);
}

void SaveGame::SetInt(std::string key, std::int64_t value) {
    entries_[std::move(key)] = SaveValue::MakeInt(value);
}

void SaveGame::SetFloat(std::string key, double value) {
    entries_[std::move(key)] = SaveValue::MakeFloat(value);
}

void SaveGame::SetString(std::string key, std::string value) {
    entries_[std::move(key)] = SaveValue::MakeString(std::move(value));
}

bool SaveGame::GetBool(std::string_view key, bool& out) const {
    const SaveValue* value = FindTyped(entries_, key, SaveValueType::Bool);
    if (value == nullptr) {
        return false;
    }
    out = value->boolValue;
    return true;
}

bool SaveGame::GetInt(std::string_view key, std::int64_t& out) const {
    const SaveValue* value = FindTyped(entries_, key, SaveValueType::Int);
    if (value == nullptr) {
        return false;
    }
    out = value->intValue;
    return true;
}

bool SaveGame::GetFloat(std::string_view key, double& out) const {
    const SaveValue* value = FindTyped(entries_, key, SaveValueType::Float);
    if (value == nullptr) {
        return false;
    }
    out = value->floatValue;
    return true;
}

bool SaveGame::GetString(std::string_view key, std::string& out) const {
    const SaveValue* value = FindTyped(entries_, key, SaveValueType::String);
    if (value == nullptr) {
        return false;
    }
    out = value->stringValue;
    return true;
}

bool SaveGame::Has(std::string_view key) const noexcept {
    return entries_.find(std::string{ key }) != entries_.end();
}

bool SaveGame::TryGetType(std::string_view key, SaveValueType& out) const noexcept {
    const auto iterator = entries_.find(std::string{ key });
    if (iterator == entries_.end()) {
        return false;
    }
    out = iterator->second.type;
    return true;
}

bool SaveGame::Remove(std::string_view key) noexcept {
    return entries_.erase(std::string{ key }) > 0;
}

void SaveGame::Clear() noexcept {
    entries_.clear();
}

std::size_t SaveGame::Count() const noexcept {
    return entries_.size();
}

} // namespace kb::save
