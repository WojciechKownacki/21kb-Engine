#pragma once

#include "engine/visual/VisualGraphCompiler.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kb::visual {

struct VisualGraphPinSignature {
    std::string name;
    VisualGraphValueType type = VisualGraphValueType::Void;
    bool required = true;
};

class VisualGraphBindingSignatureValidator {
public:
    VisualGraphBindingSignatureValidator() = delete;

    static void Validate(
        std::string_view bindingSymbol,
        const std::vector<VisualGraphPinSignature>& inputs,
        const std::vector<VisualGraphPinSignature>& outputs,
        const VisualGraphIrInstruction& instruction,
        std::vector<std::string>& errors);

private:
    [[nodiscard]] static const VisualGraphIrInput* FindInput(const VisualGraphIrInstruction& instruction, std::string_view name) noexcept;
    [[nodiscard]] static const VisualGraphIrOutput* FindOutput(const VisualGraphIrInstruction& instruction, std::string_view name) noexcept;
};

using VisualGraphNativePinSignature = VisualGraphPinSignature;

} // namespace kb::visual
