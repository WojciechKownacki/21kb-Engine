#include "engine/visual/VisualGraphBindingSignature.hpp"

#include <ranges>

namespace kb::visual {

void VisualGraphBindingSignatureValidator::Validate(
    std::string_view bindingSymbol,
    const std::vector<VisualGraphPinSignature>& inputs,
    const std::vector<VisualGraphPinSignature>& outputs,
    const VisualGraphIrInstruction& instruction,
    std::vector<std::string>& errors) {
    const std::string symbol{bindingSymbol};

    for (const VisualGraphPinSignature& input : inputs) {
        const VisualGraphIrInput* irInput = FindInput(instruction, input.name);
        if (irInput == nullptr) {
            if (input.required) {
                errors.push_back("binding '" + symbol + "' requires missing input '" + input.name + "'");
            }
            continue;
        }
        if (input.type != VisualGraphValueType::Void && irInput->type != input.type) {
            errors.push_back("binding '" + symbol + "' input '" + input.name + "' expects " + ToString(input.type) + " but graph provides " + ToString(irInput->type));
        }
    }

    for (const VisualGraphPinSignature& output : outputs) {
        const VisualGraphIrOutput* irOutput = FindOutput(instruction, output.name);
        if (irOutput == nullptr) {
            if (output.required) {
                errors.push_back("binding '" + symbol + "' requires missing output '" + output.name + "'");
            }
            continue;
        }
        if (output.type != VisualGraphValueType::Void && irOutput->type != output.type) {
            errors.push_back("binding '" + symbol + "' output '" + output.name + "' expects " + ToString(output.type) + " but graph provides " + ToString(irOutput->type));
        }
    }
}

const VisualGraphIrInput* VisualGraphBindingSignatureValidator::FindInput(const VisualGraphIrInstruction& instruction, std::string_view name) noexcept {
    const auto iter = std::ranges::find_if(instruction.inputs, [name](const VisualGraphIrInput& input) {
        return input.name == name;
    });
    return iter == instruction.inputs.end() ? nullptr : &*iter;
}

const VisualGraphIrOutput* VisualGraphBindingSignatureValidator::FindOutput(const VisualGraphIrInstruction& instruction, std::string_view name) noexcept {
    const auto iter = std::ranges::find_if(instruction.outputs, [name](const VisualGraphIrOutput& output) {
        return output.name == name;
    });
    return iter == instruction.outputs.end() ? nullptr : &*iter;
}

} // namespace kb::visual
