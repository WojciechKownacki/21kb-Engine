#pragma once

#include "engine/assets/AssetId.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace kb::editor {

struct MaterialGraphDragNodeStart {
    std::uint32_t nodeId = 0U;
    std::int32_t positionX = 0;
    std::int32_t positionY = 0;
};

struct MaterialGraphNodeDragState final {
    kb::assets::AssetId assetId{};
    std::uint32_t nodeId = 0U;
    int startX = 0;
    int startY = 0;
    int startOffsetX = 0;
    int startOffsetY = 0;
    int startNodeX = 0;
    int startNodeY = 0;
    std::optional<kb::render::RenderMaterialAssetData> startDocument;
    std::uint32_t startSelectedNodeId = 0U;
    std::vector<std::uint32_t> startSelectedNodeIds;
    std::vector<MaterialGraphDragNodeStart> startNodes;
    bool changed = false;
    bool dragging = false;
};

struct MaterialGraphCommentDragState final {
    kb::assets::AssetId assetId{};
    std::uint32_t commentId = 0U;
    int startX = 0;
    int startY = 0;
    int startCommentX = 0;
    int startCommentY = 0;
    std::optional<kb::render::RenderMaterialAssetData> startDocument;
    std::uint32_t startSelectedNodeId = 0U;
    std::vector<std::uint32_t> startSelectedNodeIds;
    std::vector<std::uint32_t> memberNodeIds;
    std::uint32_t startSelectedCommentId = 0U;
    bool changed = false;
    bool dragging = false;
};

} // namespace kb::editor
