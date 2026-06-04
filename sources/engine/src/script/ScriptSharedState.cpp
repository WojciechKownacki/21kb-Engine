#include "engine/script/ScriptSharedState.hpp"

#include <utility>

namespace kb::script {

bool ScriptSharedState::Set(std::string key, ScriptValue value) {
    if (key.empty()) {
        return false;
    }
    values_[std::move(key)] = std::move(value);
    return true;
}

bool ScriptSharedState::Has(std::string_view key) const {
    return values_.contains(std::string{key});
}

std::optional<ScriptValue> ScriptSharedState::Get(std::string_view key) const {
    const auto iter = values_.find(std::string{key});
    if (iter == values_.end()) {
        return std::nullopt;
    }
    return iter->second;
}

bool ScriptSharedState::Remove(std::string_view key) {
    return values_.erase(std::string{key}) > 0U;
}

void ScriptSharedState::Clear() noexcept {
    values_.clear();
}

std::size_t ScriptSharedState::Count() const noexcept {
    return values_.size();
}

} // namespace kb::script
