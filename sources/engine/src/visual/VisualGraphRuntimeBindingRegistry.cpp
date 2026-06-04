#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"

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

bool VisualGraphRuntimeBindingRegistry::Register(VisualGraphRuntimeBinding binding) {
    if (binding.symbol.empty() || binding.callback == nullptr || Find(binding.opcode, binding.symbol) != nullptr) {
        return false;
    }
    if (!HasValidDataPins(binding.inputs) || !HasValidDataPins(binding.outputs)) {
        return false;
    }
    bindings_.push_back(std::move(binding));
    return true;
}

const VisualGraphRuntimeBinding* VisualGraphRuntimeBindingRegistry::Find(VisualGraphIrOpcode opcode, std::string_view symbol) const noexcept {
    const auto iter = std::ranges::find_if(bindings_, [opcode, symbol](const VisualGraphRuntimeBinding& binding) {
        return binding.opcode == opcode && binding.symbol == symbol;
    });
    return iter == bindings_.end() ? nullptr : &*iter;
}

const std::vector<VisualGraphRuntimeBinding>& VisualGraphRuntimeBindingRegistry::Bindings() const noexcept {
    return bindings_;
}

} // namespace kb::visual
