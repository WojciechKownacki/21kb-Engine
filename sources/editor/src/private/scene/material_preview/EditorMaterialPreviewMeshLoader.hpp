#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/IAssetLoader.hpp"

namespace kb::editor {

class EditorMaterialPreviewMeshLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] static kb::assets::AssetId PreviewMeshAssetId() noexcept;

    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
};

} // namespace kb::editor
