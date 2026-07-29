#pragma once

#include "engine/library/EngineLibraryDeterminism.hpp"
#include "engine/visual/VisualGraphNativeBindingRegistry.hpp"
#include "engine/visual/VisualGraphNodeDefinitionRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kb::visual {

enum class VisualGraphNodeCatalogSource : std::uint8_t {
    BuiltIn,
    NativeBinding,
    RuntimeBinding,
};

struct VisualGraphNodeCatalogEntry {
    std::string id;
    std::string displayName;
    std::string category;
    VisualGraphNodeKind kind = VisualGraphNodeKind::Comment;
    VisualGraphLifecycleEvent lifecycle = VisualGraphLifecycleEvent::Tick;
    std::string symbol;
    VisualGraphNodeCatalogSource source = VisualGraphNodeCatalogSource::BuiltIn;
    kb::library::LibraryFunctionDeterminismInfo determinism;
    std::vector<VisualGraphPinTemplate> pins;
};

class VisualGraphNodeCatalog final {
public:
    [[nodiscard]] static VisualGraphNodeCatalog CreateDefault();
    [[nodiscard]] static VisualGraphNodeCatalog FromNativeBindings(const VisualGraphNativeBindingRegistry& bindings);

    [[nodiscard]] bool Register(VisualGraphNodeCatalogEntry entry);
    void RegisterBuiltInDefinitions(const VisualGraphNodeDefinitionRegistry& definitions);
    void RegisterNativeBindings(const VisualGraphNativeBindingRegistry& bindings);
    void RegisterRuntimeBindings(const VisualGraphRuntimeBindingRegistry& bindings);

    [[nodiscard]] const VisualGraphNodeCatalogEntry* Find(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<VisualGraphNodeCatalogEntry>& Entries() const noexcept;
    [[nodiscard]] std::vector<VisualGraphPin> CreatePinsForNode(std::uint32_t nodeId, std::string_view entryId) const;

private:
    std::vector<VisualGraphNodeCatalogEntry> entries_;
};

[[nodiscard]] const char* ToString(VisualGraphNodeCatalogSource source) noexcept;

} // namespace kb::visual
