#pragma once

#include "engine/scene/AIBrainComponent.hpp"

#include <charconv>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

// AI Brain fields by the names prefabs, overrides and scripts use, as text: enums by their names
// ("Melee", "KnowsEnemy", "UseAbility"), so a prefab file reads like the rule list.
struct AIBrainTextField {
    std::string name;
    std::string (*get)(const AIBrainComponent&, std::size_t rule);
    bool (*set)(AIBrainComponent&, std::size_t rule, std::string_view);
    std::size_t rule = 0U;
};

namespace ai_brain_fields {

[[nodiscard]] inline std::string Number(float value) {
    char buffer[32]{};
    const auto [end, code] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return code == std::errc{} ? std::string{ buffer, end } : std::string{ "0" };
}

[[nodiscard]] inline bool ParseNumber(std::string_view text, float& output) {
    float value = 0.0F;
    const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (code != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) return false;
    output = value;
    return true;
}

template <typename Enum, std::size_t Count>
[[nodiscard]] inline bool ParseName(std::string_view text, const std::array<std::string_view, Count>& names, Enum& output) {
    for (std::size_t index = 0U; index < Count; ++index) {
        if (names[index] == text) {
            output = static_cast<Enum>(index);
            return true;
        }
    }
    return false;
}

} // namespace ai_brain_fields

[[nodiscard]] inline const std::vector<AIBrainTextField>& AIBrainTextFields() {
    using namespace ai_brain_fields;
    static const std::vector<AIBrainTextField> fields = [] {
        std::vector<AIBrainTextField> list{
            { "preset", [](const AIBrainComponent& b, std::size_t) { return std::string{ kAIBrainPresetNames[static_cast<std::size_t>(b.preset)] }; },
              [](AIBrainComponent& b, std::size_t, std::string_view t) { return ParseName(t, kAIBrainPresetNames, b.preset); } },
            { "moveSpeed", [](const AIBrainComponent& b, std::size_t) { return Number(b.moveSpeed); },
              [](AIBrainComponent& b, std::size_t, std::string_view t) { return ParseNumber(t, b.moveSpeed); } },
            { "wanderRadius", [](const AIBrainComponent& b, std::size_t) { return Number(b.wanderRadius); },
              [](AIBrainComponent& b, std::size_t, std::string_view t) { return ParseNumber(t, b.wanderRadius); } },
            { "leashDistance", [](const AIBrainComponent& b, std::size_t) { return Number(b.leashDistance); },
              [](AIBrainComponent& b, std::size_t, std::string_view t) { return ParseNumber(t, b.leashDistance); } },
        };
        for (std::size_t rule = 0U; rule < kAIBrainRuleCount; ++rule) {
            const std::string prefix = "rule" + std::to_string(rule + 1U);
            list.push_back({ prefix + "When",
                [](const AIBrainComponent& b, std::size_t r) { return std::string{ kAIConditionNames[static_cast<std::size_t>(b.rules[r].when)] }; },
                [](AIBrainComponent& b, std::size_t r, std::string_view t) { return ParseName(t, kAIConditionNames, b.rules[r].when); }, rule });
            list.push_back({ prefix + "WhenValue",
                [](const AIBrainComponent& b, std::size_t r) { return Number(b.rules[r].whenValue); },
                [](AIBrainComponent& b, std::size_t r, std::string_view t) { return ParseNumber(t, b.rules[r].whenValue); }, rule });
            list.push_back({ prefix + "Do",
                [](const AIBrainComponent& b, std::size_t r) { return std::string{ kAIActionNames[static_cast<std::size_t>(b.rules[r].action)] }; },
                [](AIBrainComponent& b, std::size_t r, std::string_view t) { return ParseName(t, kAIActionNames, b.rules[r].action); }, rule });
            list.push_back({ prefix + "DoValue",
                [](const AIBrainComponent& b, std::size_t r) { return Number(b.rules[r].actionValue); },
                [](AIBrainComponent& b, std::size_t r, std::string_view t) { return ParseNumber(t, b.rules[r].actionValue); }, rule });
        }
        list.push_back({ "enabled", [](const AIBrainComponent& b, std::size_t) { return std::string{ b.enabled ? "1" : "0" }; },
            [](AIBrainComponent& b, std::size_t, std::string_view t) {
                if (t == "1" || t == "true") { b.enabled = true; return true; }
                if (t == "0" || t == "false") { b.enabled = false; return true; }
                return false;
            } });
        return list;
    }();
    return fields;
}

} // namespace kb::scene
