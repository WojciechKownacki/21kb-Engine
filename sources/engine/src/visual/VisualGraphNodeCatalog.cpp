#include "engine/visual/VisualGraphNodeCatalog.hpp"

#include <ranges>
#include <utility>

namespace kb::visual {
namespace {

[[nodiscard]] VisualGraphNodeKind NodeKindFor(VisualGraphIrOpcode opcode) noexcept {
    switch (opcode) {
    case VisualGraphIrOpcode::GetComponent:
        return VisualGraphNodeKind::GetComponent;
    case VisualGraphIrOpcode::GetProperty:
        return VisualGraphNodeKind::GetProperty;
    case VisualGraphIrOpcode::SetProperty:
        return VisualGraphNodeKind::SetProperty;
    case VisualGraphIrOpcode::CallNative:
        return VisualGraphNodeKind::CallNative;
    case VisualGraphIrOpcode::EmitEvent:
        return VisualGraphNodeKind::EmitEvent;
    case VisualGraphIrOpcode::Wait:
        return VisualGraphNodeKind::Wait;
    case VisualGraphIrOpcode::Branch:
        return VisualGraphNodeKind::Branch;
    case VisualGraphIrOpcode::Sequence:
        return VisualGraphNodeKind::Sequence;
    }
    return VisualGraphNodeKind::Comment;
}

[[nodiscard]] std::string EntryIdFor(VisualGraphNodeCatalogSource source, VisualGraphIrOpcode opcode, std::string_view symbol) {
    return std::string{ToString(source)} + ":" + ToString(opcode) + ":" + std::string{symbol};
}

[[nodiscard]] bool HasExecutionPins(VisualGraphIrOpcode opcode) noexcept {
    return opcode == VisualGraphIrOpcode::CallNative || opcode == VisualGraphIrOpcode::SetProperty || opcode == VisualGraphIrOpcode::GetComponent ||
           opcode == VisualGraphIrOpcode::EmitEvent;
}

void AppendBindingPins(
    std::vector<VisualGraphPinTemplate>& pins,
    VisualGraphIrOpcode opcode,
    const std::vector<VisualGraphPinSignature>& inputs,
    const std::vector<VisualGraphPinSignature>& outputs) {
    if (HasExecutionPins(opcode)) {
        pins.push_back(VisualGraphPinTemplate{
            .direction = VisualGraphPinDirection::Input,
            .name = "exec",
            .type = VisualGraphValueType::Void,
        });
    }

    for (const VisualGraphPinSignature& input : inputs) {
        pins.push_back(VisualGraphPinTemplate{
            .direction = VisualGraphPinDirection::Input,
            .name = input.name,
            .type = input.type,
        });
    }
    for (const VisualGraphPinSignature& output : outputs) {
        pins.push_back(VisualGraphPinTemplate{
            .direction = VisualGraphPinDirection::Output,
            .name = output.name,
            .type = output.type,
        });
    }

    if (HasExecutionPins(opcode)) {
        pins.push_back(VisualGraphPinTemplate{
            .direction = VisualGraphPinDirection::Output,
            .name = "then",
            .type = VisualGraphValueType::Void,
        });
    }
}

[[nodiscard]] VisualGraphNodeCatalogEntry EntryFromNativeBinding(const VisualGraphNativeBinding& binding) {
    VisualGraphNodeCatalogEntry entry{
        .id = EntryIdFor(VisualGraphNodeCatalogSource::NativeBinding, binding.opcode, binding.symbol),
        .displayName = binding.symbol,
        .category = std::string{"Native/"} + ToString(binding.opcode),
        .kind = NodeKindFor(binding.opcode),
        .symbol = binding.symbol,
        .source = VisualGraphNodeCatalogSource::NativeBinding,
        .determinism = kb::library::ClassifyLibraryFunctionDeterminism(binding.symbol),
    };
    AppendBindingPins(entry.pins, binding.opcode, binding.inputs, binding.outputs);
    return entry;
}

[[nodiscard]] VisualGraphNodeCatalogEntry EntryFromRuntimeBinding(const VisualGraphRuntimeBinding& binding) {
    VisualGraphNodeCatalogEntry entry{
        .id = EntryIdFor(VisualGraphNodeCatalogSource::RuntimeBinding, binding.opcode, binding.symbol),
        .displayName = binding.symbol,
        .category = std::string{"Runtime/"} + ToString(binding.opcode),
        .kind = NodeKindFor(binding.opcode),
        .symbol = binding.symbol,
        .source = VisualGraphNodeCatalogSource::RuntimeBinding,
        .determinism = kb::library::ClassifyLibraryFunctionDeterminism(binding.symbol),
    };
    AppendBindingPins(entry.pins, binding.opcode, binding.inputs, binding.outputs);
    return entry;
}

} // namespace

VisualGraphNodeCatalog VisualGraphNodeCatalog::CreateDefault() {
    VisualGraphNodeCatalog catalog;
    catalog.RegisterBuiltInDefinitions(VisualGraphNodeDefinitionRegistry::CreateDefault());
    return catalog;
}

VisualGraphNodeCatalog VisualGraphNodeCatalog::FromNativeBindings(const VisualGraphNativeBindingRegistry& bindings) {
    VisualGraphNodeCatalog catalog = CreateDefault();
    catalog.RegisterNativeBindings(bindings);
    return catalog;
}

bool VisualGraphNodeCatalog::Register(VisualGraphNodeCatalogEntry entry) {
    if (entry.id.empty() || entry.displayName.empty() || Find(entry.id) != nullptr) {
        return false;
    }
    entries_.push_back(std::move(entry));
    return true;
}

void VisualGraphNodeCatalog::RegisterBuiltInDefinitions(const VisualGraphNodeDefinitionRegistry& definitions) {
    for (const VisualGraphNodeDefinition& definition : definitions.Definitions()) {
        static_cast<void>(Register(VisualGraphNodeCatalogEntry{
            .id = std::string{"BuiltIn:"} + ToString(definition.kind),
            .displayName = definition.displayName,
            .category = "Flow",
            .kind = definition.kind,
            .source = VisualGraphNodeCatalogSource::BuiltIn,
            .pins = definition.pins,
        }));
    }
}

void VisualGraphNodeCatalog::RegisterNativeBindings(const VisualGraphNativeBindingRegistry& bindings) {
    for (const VisualGraphNativeBinding& binding : bindings.Bindings()) {
        static_cast<void>(Register(EntryFromNativeBinding(binding)));
    }
}

void VisualGraphNodeCatalog::RegisterRuntimeBindings(const VisualGraphRuntimeBindingRegistry& bindings) {
    for (const VisualGraphRuntimeBinding& binding : bindings.Bindings()) {
        VisualGraphNodeCatalogEntry entry = EntryFromRuntimeBinding(binding);
        const auto nativeEquivalent = std::ranges::find_if(entries_, [&entry](const VisualGraphNodeCatalogEntry& existing) {
            return existing.source == VisualGraphNodeCatalogSource::NativeBinding && existing.kind == entry.kind && existing.symbol == entry.symbol;
        });
        if (nativeEquivalent == entries_.end()) {
            static_cast<void>(Register(std::move(entry)));
        }
    }
}

const VisualGraphNodeCatalogEntry* VisualGraphNodeCatalog::Find(std::string_view id) const noexcept {
    const auto iter = std::ranges::find_if(entries_, [id](const VisualGraphNodeCatalogEntry& entry) {
        return entry.id == id;
    });
    return iter == entries_.end() ? nullptr : &*iter;
}

const std::vector<VisualGraphNodeCatalogEntry>& VisualGraphNodeCatalog::Entries() const noexcept {
    return entries_;
}

std::vector<VisualGraphPin> VisualGraphNodeCatalog::CreatePinsForNode(std::uint32_t nodeId, std::string_view entryId) const {
    const VisualGraphNodeCatalogEntry* entry = Find(entryId);
    if (entry == nullptr) {
        return {};
    }

    std::vector<VisualGraphPin> pins;
    pins.reserve(entry->pins.size());
    for (const VisualGraphPinTemplate& pinTemplate : entry->pins) {
        pins.push_back(VisualGraphPin{
            .nodeId = nodeId,
            .direction = pinTemplate.direction,
            .name = pinTemplate.name,
            .type = pinTemplate.type,
        });
    }
    return pins;
}

const char* ToString(VisualGraphNodeCatalogSource source) noexcept {
    switch (source) {
    case VisualGraphNodeCatalogSource::BuiltIn:
        return "BuiltIn";
    case VisualGraphNodeCatalogSource::NativeBinding:
        return "NativeBinding";
    case VisualGraphNodeCatalogSource::RuntimeBinding:
        return "RuntimeBinding";
    }
    return "BuiltIn";
}

} // namespace kb::visual
