#pragma once

#include "engine/script/ScriptValue.hpp"
#include "engine/visual/VisualGraphDiagnostic.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::script {

enum class ScriptApiNameKind {
    SharedKey,
    Event,
    Function,
    ExposedVariable,
};

struct ScriptApiPin {
    std::string name;
    ScriptValueType type = ScriptValueType::Void;
    bool required = true;
};

struct ScriptApiNameEntry {
    ScriptApiNameKind kind = ScriptApiNameKind::SharedKey;
    std::string name;
    std::string owner;
    ScriptValueType valueType = ScriptValueType::Void;
    std::vector<ScriptApiPin> inputs;
    std::vector<ScriptApiPin> outputs;
    bool hasValueTypeContract = false;
    bool hasInputContract = false;
    bool hasOutputContract = false;
    bool declaresProvider = false;
};

struct ScriptApiNameValidationResult {
    std::vector<std::string> errors;
    std::vector<kb::visual::VisualGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty() && !kb::visual::VisualGraphDiagnostics::HasErrors(diagnostics);
    }
};

class ScriptApiNameRegistry final {
public:
    [[nodiscard]] bool Register(ScriptApiNameKind kind, std::string name, std::string owner = {});
    [[nodiscard]] bool RegisterEntry(ScriptApiNameEntry entry);
    [[nodiscard]] bool RegisterSharedKey(std::string name, ScriptValueType valueType = ScriptValueType::Void, std::string owner = {});
    [[nodiscard]] bool RegisterExposedVariable(std::string name, ScriptValueType valueType, std::string owner = {});
    [[nodiscard]] bool RegisterEvent(std::string name, std::span<const ScriptApiPin> payload = {}, std::string owner = {});
    [[nodiscard]] bool RegisterFunction(
        std::string name,
        std::span<const ScriptApiPin> inputs = {},
        std::span<const ScriptApiPin> outputs = {},
        std::string owner = {},
        bool declaresProvider = false);
    [[nodiscard]] bool Contains(ScriptApiNameKind kind, std::string_view name) const noexcept;
    [[nodiscard]] ScriptApiNameValidationResult Validate(bool disallowCrossKindCollisions = false) const;
    [[nodiscard]] const std::vector<ScriptApiNameEntry>& Entries() const noexcept;
    void Clear() noexcept;

private:
    std::vector<ScriptApiNameEntry> entries_;
};

[[nodiscard]] const char* ToString(ScriptApiNameKind kind) noexcept;

} // namespace kb::script
