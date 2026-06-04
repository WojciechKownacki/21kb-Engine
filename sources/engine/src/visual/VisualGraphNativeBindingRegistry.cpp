#include "engine/visual/VisualGraphNativeBindingRegistry.hpp"

#include <ranges>
#include <set>
#include <utility>

namespace kb::visual {
namespace {

[[nodiscard]] bool HasValidDataPins(const std::vector<VisualGraphPinSignature>& pins) {
    std::set<std::string_view> names;
    for (const VisualGraphPinSignature& pin : pins) {
        if (pin.name.empty() || pin.type == VisualGraphValueType::Void) {
            return false;
        }
        if (!names.insert(pin.name).second) {
            return false;
        }
    }
    return true;
}

} // namespace

bool VisualGraphNativeBindingRegistry::Register(VisualGraphNativeBinding binding) {
    if (binding.symbol.empty() || (binding.statement.empty() && binding.functionName.empty())) {
        return false;
    }
    if (!HasValidDataPins(binding.inputs) || !HasValidDataPins(binding.outputs)) {
        return false;
    }
    if (Find(binding.opcode, binding.symbol) != nullptr) {
        return false;
    }

    bindings_.push_back(std::move(binding));
    return true;
}

const VisualGraphNativeBinding* VisualGraphNativeBindingRegistry::Find(VisualGraphIrOpcode opcode, std::string_view symbol) const noexcept {
    const auto matchesBinding = [opcode, symbol](const VisualGraphNativeBinding& binding) {
        return binding.opcode == opcode && binding.symbol == symbol;
    };
    const auto iter = std::ranges::find_if(bindings_, matchesBinding);
    return iter == bindings_.end() ? nullptr : &(*iter);
}

const std::vector<VisualGraphNativeBinding>& VisualGraphNativeBindingRegistry::Bindings() const noexcept {
    return bindings_;
}

} // namespace kb::visual
