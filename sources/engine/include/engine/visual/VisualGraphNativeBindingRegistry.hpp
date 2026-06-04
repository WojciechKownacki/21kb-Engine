#pragma once

#include "engine/visual/VisualGraphBindingSignature.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kb::visual {

struct VisualGraphNativeBinding {
    VisualGraphIrOpcode opcode = VisualGraphIrOpcode::CallNative;
    std::string symbol;
    std::string functionName;
    std::string statement;
    std::vector<VisualGraphPinSignature> inputs;
    std::vector<VisualGraphPinSignature> outputs;
    bool passContext = true;
};

class VisualGraphNativeBindingRegistry final {
public:
    [[nodiscard]] bool Register(VisualGraphNativeBinding binding);
    [[nodiscard]] const VisualGraphNativeBinding* Find(VisualGraphIrOpcode opcode, std::string_view symbol) const noexcept;
    [[nodiscard]] const std::vector<VisualGraphNativeBinding>& Bindings() const noexcept;

private:
    std::vector<VisualGraphNativeBinding> bindings_;
};

} // namespace kb::visual
