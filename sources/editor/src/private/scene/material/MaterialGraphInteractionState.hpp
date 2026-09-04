#pragma once

#include "engine/assets/AssetId.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "rendering/material_graph/MaterialGraphInteractionPolicy.hpp"
#include "scene/material/MaterialEditorGraphModels.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
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

enum class MaterialGraphSelectionOperation : std::uint8_t {
    Replace,
    Add,
    Invert,
    Remove,
};

struct MaterialGraphBoxSelectionState final {
    kb::assets::AssetId assetId{};
    int startX = 0;
    int startY = 0;
    int currentX = 0;
    int currentY = 0;
    MaterialGraphSelectionOperation operation = MaterialGraphSelectionOperation::Replace;
    std::vector<std::uint32_t> baseNodeIds;
    std::uint32_t basePrimaryNodeId = 0U;
    bool moved = false;
    bool selecting = false;
};

struct MaterialGraphViewState {
    float zoom = MaterialGraphInteractionPolicy::DefaultZoom;
    int panX = 0;
    int panY = 0;
};

struct MaterialGraphViewportState final {
    std::unordered_map<std::uint64_t, MaterialGraphViewState> viewStates;
    float zoom = MaterialGraphInteractionPolicy::DefaultZoom;
    int panX = 0;
    int panY = 0;
    int canvasWidth = 1280;
    int canvasHeight = 720;
    int canvasLeft = 0;
    int canvasTop = 0;
    bool focused = false;
    int panStartX = 0;
    int panStartY = 0;
    int panStartOffsetX = 0;
    int panStartOffsetY = 0;
    bool panning = false;
    bool panMoved = false;
};

struct MaterialGraphContextMenuState final {
    kb::assets::AssetId assetId{};
    int x = 0;
    int y = 0;
    int graphX = 0;
    int graphY = 0;
    int scrollOffset = 0;
    std::uint32_t expandedMask = 0U;
    std::size_t hoveredCategory = static_cast<std::size_t>(-1);
    MaterialEditorGraphMenuCommand hoveredCommand = MaterialEditorGraphMenuCommand::None;
    std::string searchQuery;
    std::vector<MaterialEditorGraphMenuCommand> paletteFavorites;
    std::uint32_t pinFilterNodeId = 0U;
    std::string pinFilterPin;
    bool pinFilterOutput = true;
    bool pinFilterActive = false;
};

struct MaterialGraphTexturePickerState final {
    kb::assets::AssetId assetId{};
    std::uint32_t nodeId = 0U;
    kb::assets::AssetId selectedTextureId{};
    std::string searchQuery;
    int scrollOffset = 0;
};

} // namespace kb::editor
