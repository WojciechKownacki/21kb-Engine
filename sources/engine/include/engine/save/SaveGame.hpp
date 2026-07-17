#pragma once

#include "engine/save/SaveValue.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kb::save {

// LIB-162: an in-memory save-game document — a flat string -> scalar table,
// the persistent counterpart to kb::script::ScriptSharedState. Typed setters
// overwrite any existing entry (including one of a different type) for the
// same key; typed getters return false and leave the out-parameter untouched
// when the key is absent OR holds a different type (never a silent coercion —
// asking for an Int that was stored as a String is a miss, not a 0). Serialize
// it through kb::save::SaveGameService.
class SaveGame {
public:
    void SetBool(std::string key, bool value);
    void SetInt(std::string key, std::int64_t value);
    void SetFloat(std::string key, double value);
    void SetString(std::string key, std::string value);

    [[nodiscard]] bool GetBool(std::string_view key, bool& out) const;
    [[nodiscard]] bool GetInt(std::string_view key, std::int64_t& out) const;
    [[nodiscard]] bool GetFloat(std::string_view key, double& out) const;
    [[nodiscard]] bool GetString(std::string_view key, std::string& out) const;

    [[nodiscard]] bool Has(std::string_view key) const noexcept;
    // The type stored under `key`, or false if the key is absent.
    [[nodiscard]] bool TryGetType(std::string_view key, SaveValueType& out) const noexcept;
    [[nodiscard]] bool Remove(std::string_view key) noexcept;
    void Clear() noexcept;
    [[nodiscard]] std::size_t Count() const noexcept;

    // Direct access to the entry table — for the serializer and migration.
    [[nodiscard]] const std::unordered_map<std::string, SaveValue>& Entries() const noexcept {
        return entries_;
    }
    void SetEntries(std::unordered_map<std::string, SaveValue> entries) {
        entries_ = std::move(entries);
    }

private:
    std::unordered_map<std::string, SaveValue> entries_;
};

} // namespace kb::save
