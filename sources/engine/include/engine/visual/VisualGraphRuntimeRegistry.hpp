#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/visual/VisualGraphCompiler.hpp"
#include "engine/visual/VisualGraphGeneratedCodeWriter.hpp"
#include "engine/visual/VisualGraphNativeCodeGenerator.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace kb::visual {

struct VisualGraphRuntimeArtifact {
    kb::assets::AssetId assetId{};
    std::string graphName;
    VisualGraphIrModule module;
    VisualGraphNativeCode nativeCode;
    VisualGraphGeneratedCodeFiles generatedFiles;
};

class VisualGraphRuntimeRegistry final {
public:
    void Store(VisualGraphRuntimeArtifact artifact);
    [[nodiscard]] const VisualGraphRuntimeArtifact* Find(kb::assets::AssetId assetId) const noexcept;
    [[nodiscard]] VisualGraphRuntimeArtifact* FindMutable(kb::assets::AssetId assetId) noexcept;
    [[nodiscard]] bool Contains(kb::assets::AssetId assetId) const noexcept;
    [[nodiscard]] std::size_t Count() const noexcept;
    void Remove(kb::assets::AssetId assetId) noexcept;
    void Clear() noexcept;

private:
    std::unordered_map<std::uint64_t, VisualGraphRuntimeArtifact> artifacts_;
};

} // namespace kb::visual
