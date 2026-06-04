#pragma once

#include "engine/visual/VisualGraphDiagnostic.hpp"
#include "engine/visual/VisualGraphTypes.hpp"

#include <string>
#include <vector>

namespace kb::visual {

enum class VisualGraphIrOpcode : std::uint8_t {
    Sequence,
    Branch,
    GetComponent,
    GetProperty,
    SetProperty,
    CallNative,
    EmitEvent,
};

struct VisualGraphIrInput {
    std::string name;
    VisualGraphValueType type = VisualGraphValueType::Void;
    std::uint32_t sourceNodeId = 0;
    std::string sourcePin;
};

struct VisualGraphIrOutput {
    std::string name;
    VisualGraphValueType type = VisualGraphValueType::Void;
};

struct VisualGraphIrInstruction {
    VisualGraphIrOpcode opcode = VisualGraphIrOpcode::Sequence;
    std::uint32_t sourceNodeId = 0;
    std::string symbol;
    std::vector<VisualGraphIrInput> inputs;
    std::vector<VisualGraphIrOutput> outputs;
    std::uint32_t nextNodeId = 0;
    std::uint32_t trueNodeId = 0;
    std::uint32_t falseNodeId = 0;
};

struct VisualGraphIrFunction {
    VisualGraphLifecycleEvent event = VisualGraphLifecycleEvent::Tick;
    std::string customEventName;
    std::uint32_t eventNodeId = 0;
    std::vector<VisualGraphIrOutput> eventOutputs;
    std::uint32_t entryNodeId = 0;
    std::vector<VisualGraphIrInstruction> instructions;
};

struct VisualGraphIrModule {
    std::string graphName;
    std::vector<VisualGraphVariable> variables;
    std::vector<VisualGraphIrFunction> functions;
};

struct VisualGraphCompileResult {
    VisualGraphIrModule module;
    std::vector<std::string> errors;
    std::vector<VisualGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty() && !VisualGraphDiagnostics::HasErrors(diagnostics);
    }
};

class VisualGraphCompiler {
public:
    VisualGraphCompiler() = delete;

    [[nodiscard]] static VisualGraphCompileResult Compile(const VisualGraphAsset& graph);
};

[[nodiscard]] const char* ToString(VisualGraphIrOpcode opcode) noexcept;

} // namespace kb::visual
