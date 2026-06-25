#pragma once

#include "inspection/InspectorPanelState.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "engine/assets/AssetId.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kb::editor {

class MaterialEditorState {
public:
    [[nodiscard]] kb::assets::AssetId OpenAssetId() const noexcept {
        return openAssetId_;
    }

    [[nodiscard]] bool HasOpenAsset() const noexcept {
        return openAssetId_.IsValid();
    }

    [[nodiscard]] const std::optional<kb::render::RenderMaterialAssetData>& WorkingCopy() const noexcept {
        return workingCopy_;
    }

    [[nodiscard]] const std::optional<kb::render::RenderMaterialAssetData>& CleanSnapshot() const noexcept {
        return cleanSnapshot_;
    }

    [[nodiscard]] bool Dirty() const noexcept {
        return dirty_;
    }

    [[nodiscard]] const std::vector<std::string>& Diagnostics() const noexcept {
        return diagnostics_;
    }

    [[nodiscard]] bool DiagnosticsHaveError() const noexcept {
        return diagnosticsHaveError_;
    }

    [[nodiscard]] std::uint32_t SelectedNodeId() const noexcept {
        return selectedNodeId_;
    }

    [[nodiscard]] InspectorPropertyId SelectedParameter() const noexcept {
        return selectedParameter_;
    }

    [[nodiscard]] bool InfoPanelVisible() const noexcept {
        return infoPanelVisible_;
    }

    void Open(kb::assets::AssetId assetId, std::optional<kb::render::RenderMaterialAssetData> document) {
        openAssetId_ = assetId;
        workingCopy_ = document;
        cleanSnapshot_ = std::move(document);
        dirty_ = false;
        selectedNodeId_ = 0U;
        selectedParameter_ = InspectorPropertyId::None;
        infoPanelVisible_ = false;
        ClearDiagnostics();
    }

    void Close() noexcept {
        openAssetId_ = {};
        workingCopy_.reset();
        cleanSnapshot_.reset();
        dirty_ = false;
        selectedNodeId_ = 0U;
        selectedParameter_ = InspectorPropertyId::None;
        infoPanelVisible_ = false;
        diagnostics_.clear();
        diagnosticsHaveError_ = false;
    }

    void SetWorkingCopy(kb::render::RenderMaterialAssetData document) {
        workingCopy_ = std::move(document);
        dirty_ = true;
    }

    void MarkSaved() {
        cleanSnapshot_ = workingCopy_;
        dirty_ = false;
    }

    void RevertToCleanSnapshot() {
        workingCopy_ = cleanSnapshot_;
        dirty_ = false;
    }

    bool SelectNode(std::uint32_t nodeId) noexcept {
        if (selectedNodeId_ == nodeId) {
            return false;
        }
        selectedNodeId_ = nodeId;
        selectedParameter_ = InspectorPropertyId::None;
        return true;
    }

    bool ClearNodeSelection() noexcept {
        return SelectNode(0U);
    }

    bool SelectParameter(InspectorPropertyId property) noexcept {
        if (selectedParameter_ == property) {
            return false;
        }
        selectedParameter_ = property;
        return true;
    }

    bool ToggleInfoPanel() noexcept {
        infoPanelVisible_ = !infoPanelVisible_;
        return true;
    }

    void SetDiagnostics(std::vector<std::string> diagnostics, bool hasError) {
        diagnostics_ = std::move(diagnostics);
        diagnosticsHaveError_ = hasError;
    }

    void ClearDiagnostics() {
        diagnostics_.clear();
        diagnosticsHaveError_ = false;
    }

private:
    kb::assets::AssetId openAssetId_{};
    std::optional<kb::render::RenderMaterialAssetData> workingCopy_;
    std::optional<kb::render::RenderMaterialAssetData> cleanSnapshot_;
    std::vector<std::string> diagnostics_;
    bool diagnosticsHaveError_ = false;
    bool dirty_ = false;
    bool infoPanelVisible_ = false;
    std::uint32_t selectedNodeId_ = 0U;
    InspectorPropertyId selectedParameter_ = InspectorPropertyId::None;
};

} // namespace kb::editor
