#pragma once

#include "engine/script/ScriptValue.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kb::script {

class ScriptSharedState final {
public:
    [[nodiscard]] bool Set(std::string key, ScriptValue value);
    [[nodiscard]] bool Has(std::string_view key) const;
    [[nodiscard]] std::optional<ScriptValue> Get(std::string_view key) const;
    [[nodiscard]] bool Remove(std::string_view key);
    void Clear() noexcept;
    [[nodiscard]] std::size_t Count() const noexcept;

private:
    std::unordered_map<std::string, ScriptValue> values_;
};

} // namespace kb::script
