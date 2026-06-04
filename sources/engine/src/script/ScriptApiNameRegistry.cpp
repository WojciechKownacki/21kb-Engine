#include "engine/script/ScriptApiNameRegistry.hpp"

#include <ranges>
#include <span>
#include <utility>

namespace kb::script {
namespace {

[[nodiscard]] bool SameKindAndName(const ScriptApiNameEntry& lhs, const ScriptApiNameEntry& rhs) noexcept {
    return lhs.kind == rhs.kind && lhs.name == rhs.name;
}

[[nodiscard]] bool SamePins(std::span<const ScriptApiPin> lhs, std::span<const ScriptApiPin> rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        if (lhs[index].name != rhs[index].name || lhs[index].type != rhs[index].type || lhs[index].required != rhs[index].required) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string PinSignature(std::span<const ScriptApiPin> pins) {
    std::string signature;
    for (std::size_t index = 0U; index < pins.size(); ++index) {
        if (index > 0U) {
            signature += ", ";
        }
        signature += pins[index].name + ": " + ToString(pins[index].type);
        if (!pins[index].required) {
            signature += "?";
        }
    }
    return signature;
}

[[nodiscard]] std::string FunctionSignature(const ScriptApiNameEntry& entry) {
    return "(" + PinSignature(entry.inputs) + ") -> (" + PinSignature(entry.outputs) + ")";
}

[[nodiscard]] std::string EntryLabel(const ScriptApiNameEntry& entry) {
    std::string label = std::string{ ToString(entry.kind) } + " '" + entry.name + "'";
    if (!entry.owner.empty()) {
        label += " from " + entry.owner;
    }
    return label;
}

void AddDiagnostic(ScriptApiNameValidationResult& result, std::string message) {
    result.errors.push_back(message);
    result.diagnostics.push_back(kb::visual::VisualGraphDiagnostics::Error(kb::visual::VisualGraphDiagnosticStage::ApiNameValidation, std::move(message)));
}

void ValidateSameKindCollision(ScriptApiNameValidationResult& result, const ScriptApiNameEntry& entry, const ScriptApiNameEntry& other) {
    if (entry.kind == ScriptApiNameKind::SharedKey || entry.kind == ScriptApiNameKind::ExposedVariable) {
        if (entry.hasValueTypeContract && other.hasValueTypeContract && entry.valueType != other.valueType) {
            AddDiagnostic(result,
                "script API value contract conflict for '" + entry.name + "': " + EntryLabel(entry) + " uses " + ToString(entry.valueType) + ", but " +
                    EntryLabel(other) + " uses " + ToString(other.valueType));
        }
        return;
    }

    if (entry.kind == ScriptApiNameKind::Event) {
        if (entry.hasInputContract && other.hasInputContract && !SamePins(entry.inputs, other.inputs)) {
            AddDiagnostic(result,
                "script API event payload conflict for '" + entry.name + "': " + EntryLabel(entry) + " has (" + PinSignature(entry.inputs) + "), but " +
                    EntryLabel(other) + " has (" + PinSignature(other.inputs) + ")");
        }
        return;
    }

    if (entry.hasInputContract && other.hasInputContract && !SamePins(entry.inputs, other.inputs)) {
        AddDiagnostic(result,
            "script API function input contract conflict for '" + entry.name + "': " + EntryLabel(entry) + " has " + FunctionSignature(entry) + ", but " +
                EntryLabel(other) + " has " + FunctionSignature(other));
    }
    if (entry.hasOutputContract && other.hasOutputContract && !SamePins(entry.outputs, other.outputs)) {
        AddDiagnostic(result,
            "script API function output contract conflict for '" + entry.name + "': " + EntryLabel(entry) + " has " + FunctionSignature(entry) + ", but " +
                EntryLabel(other) + " has " + FunctionSignature(other));
    }
    if (entry.declaresProvider && other.declaresProvider) {
        AddDiagnostic(result, "script API function provider conflict for '" + entry.name + "': " + EntryLabel(entry) + " conflicts with " + EntryLabel(other));
    }
}

} // namespace

bool ScriptApiNameRegistry::Register(ScriptApiNameKind kind, std::string name, std::string owner) {
    return RegisterEntry(ScriptApiNameEntry{
        .kind = kind,
        .name = std::move(name),
        .owner = std::move(owner),
    });
}

bool ScriptApiNameRegistry::RegisterEntry(ScriptApiNameEntry entry) {
    if (entry.name.empty()) {
        return false;
    }
    entries_.push_back(std::move(entry));
    return true;
}

bool ScriptApiNameRegistry::RegisterSharedKey(std::string name, ScriptValueType valueType, std::string owner) {
    if (name.empty()) {
        return false;
    }
    entries_.push_back(ScriptApiNameEntry{
        .kind = ScriptApiNameKind::SharedKey,
        .name = std::move(name),
        .owner = std::move(owner),
        .valueType = valueType,
        .hasValueTypeContract = valueType != ScriptValueType::Void,
    });
    return true;
}

bool ScriptApiNameRegistry::RegisterExposedVariable(std::string name, ScriptValueType valueType, std::string owner) {
    if (name.empty() || valueType == ScriptValueType::Void) {
        return false;
    }
    entries_.push_back(ScriptApiNameEntry{
        .kind = ScriptApiNameKind::ExposedVariable,
        .name = std::move(name),
        .owner = std::move(owner),
        .valueType = valueType,
        .hasValueTypeContract = true,
    });
    return true;
}

bool ScriptApiNameRegistry::RegisterEvent(std::string name, std::span<const ScriptApiPin> payload, std::string owner) {
    if (name.empty()) {
        return false;
    }
    entries_.push_back(ScriptApiNameEntry{
        .kind = ScriptApiNameKind::Event,
        .name = std::move(name),
        .owner = std::move(owner),
        .inputs = std::vector<ScriptApiPin>{ payload.begin(), payload.end() },
        .hasInputContract = true,
    });
    return true;
}

bool ScriptApiNameRegistry::RegisterFunction(
    std::string name,
    std::span<const ScriptApiPin> inputs,
    std::span<const ScriptApiPin> outputs,
    std::string owner,
    bool declaresProvider) {
    if (name.empty()) {
        return false;
    }
    entries_.push_back(ScriptApiNameEntry{
        .kind = ScriptApiNameKind::Function,
        .name = std::move(name),
        .owner = std::move(owner),
        .inputs = std::vector<ScriptApiPin>{ inputs.begin(), inputs.end() },
        .outputs = std::vector<ScriptApiPin>{ outputs.begin(), outputs.end() },
        .hasInputContract = true,
        .hasOutputContract = true,
        .declaresProvider = declaresProvider,
    });
    return true;
}

bool ScriptApiNameRegistry::Contains(ScriptApiNameKind kind, std::string_view name) const noexcept {
    const auto iter = std::ranges::find_if(entries_, [kind, name](const ScriptApiNameEntry& entry) {
        return entry.kind == kind && entry.name == name;
    });
    return iter != entries_.end();
}

ScriptApiNameValidationResult ScriptApiNameRegistry::Validate(bool disallowCrossKindCollisions) const {
    ScriptApiNameValidationResult result{};
    for (std::size_t index = 0U; index < entries_.size(); ++index) {
        const ScriptApiNameEntry& entry = entries_[index];
        if (entry.name.empty()) {
            AddDiagnostic(result, "script API name entry is empty");
            continue;
        }
        for (std::size_t otherIndex = index + 1U; otherIndex < entries_.size(); ++otherIndex) {
            const ScriptApiNameEntry& other = entries_[otherIndex];
            if (SameKindAndName(entry, other)) {
                ValidateSameKindCollision(result, entry, other);
                continue;
            }
            if (disallowCrossKindCollisions && entry.name == other.name) {
                AddDiagnostic(result, "script API name '" + entry.name + "' is reused by " + std::string{ ToString(entry.kind) } + " and " + ToString(other.kind));
            }
        }
    }
    return result;
}

const std::vector<ScriptApiNameEntry>& ScriptApiNameRegistry::Entries() const noexcept {
    return entries_;
}

void ScriptApiNameRegistry::Clear() noexcept {
    entries_.clear();
}

const char* ToString(ScriptApiNameKind kind) noexcept {
    switch (kind) {
    case ScriptApiNameKind::SharedKey:
        return "SharedKey";
    case ScriptApiNameKind::Event:
        return "Event";
    case ScriptApiNameKind::Function:
        return "Function";
    case ScriptApiNameKind::ExposedVariable:
        return "ExposedVariable";
    }
    return "SharedKey";
}

} // namespace kb::script
