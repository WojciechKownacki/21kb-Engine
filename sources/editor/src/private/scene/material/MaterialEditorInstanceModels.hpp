#pragma once

#include "scene/material/MaterialEditorParameterModels.hpp"

#include "engine/assets/AssetId.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kb::editor {

struct MaterialEditorInstanceParentChainRow {
    kb::assets::AssetId assetId{};
    std::string label;
    bool current = false;
};

struct MaterialEditorInstanceOverrideGroupRow {
    MaterialEditorParameterGroup group = MaterialEditorParameterGroup::Core;
    bool expanded = true;
    std::uint32_t activeOverrideCount = 0U;
    std::uint32_t totalParameterCount = 0U;
    std::vector<MaterialEditorParameter> parameters;
};

struct MaterialEditorInstanceStaticSwitchRow {
    std::uint32_t nodeId = 0U;
    std::string stableId;
    std::string displayName;
    kb::render::RenderMaterialGraphNodeKind nodeKind = kb::render::RenderMaterialGraphNodeKind::StaticBoolParameter;
    std::string parentValue;
    std::string value;
    bool overrideActive = false;
};

struct MaterialEditorLayerTreeRow {
    std::uint32_t nodeId = 0U;
    std::size_t index = 0U;
    bool enabled = true;
    std::uint64_t layerFunctionAssetId = 0U;
    std::uint64_t blendFunctionAssetId = 0U;
    std::string layerName;
    std::string blendName;
    std::string linkState;
    std::uint32_t layerParameterCount = 0U;
    std::uint32_t blendParameterCount = 0U;
};

} // namespace kb::editor
