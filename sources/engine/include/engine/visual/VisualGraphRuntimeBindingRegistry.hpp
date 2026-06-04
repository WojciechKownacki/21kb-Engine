#pragma once

#include "engine/visual/VisualGraphBindingSignature.hpp"
#include "engine/visual/VisualGraphCompiler.hpp"
#include "engine/visual/VisualGraphRuntimeExecutionContext.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::visual {

using VisualGraphRuntimeCallback = std::function<void(VisualGraphRuntimeExecutionContext&, const VisualGraphIrInstruction&)>;

struct VisualGraphRuntimeBinding {
    VisualGraphIrOpcode opcode = VisualGraphIrOpcode::CallNative;
    std::string symbol;
    std::vector<VisualGraphPinSignature> inputs;
    std::vector<VisualGraphPinSignature> outputs;
    VisualGraphRuntimeCallback callback;
};

class VisualGraphRuntimeBindingRegistry final {
public:
    [[nodiscard]] bool Register(VisualGraphRuntimeBinding binding);
    [[nodiscard]] const VisualGraphRuntimeBinding* Find(VisualGraphIrOpcode opcode, std::string_view symbol) const noexcept;
    [[nodiscard]] const std::vector<VisualGraphRuntimeBinding>& Bindings() const noexcept;

private:
    std::vector<VisualGraphRuntimeBinding> bindings_;
};

} // namespace kb::visual
