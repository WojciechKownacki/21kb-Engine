#pragma once

#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <string>

namespace kb::editor {

struct EditorRenderSettingsRecord {
    EditorRenderBackend renderBackend = EditorRenderBackend::Auto;
    bool shadowsEnabled = true;
    bool postProcessEnabled = true;
    EditorAntiAliasingMode antiAliasingMode = EditorAntiAliasingMode::Taa;
    std::uint8_t msaaSamples = 0U;
    bool bloomEnabled = true;
    bool selectionOutlineEnabled = true;
    bool gpuDrivenEnabled = true;

    [[nodiscard]] bool operator==(const EditorRenderSettingsRecord&) const noexcept = default;
};

struct EditorWorkspacePreferences {
    bool autosaveEnabled = true;
    std::uint32_t autosaveIntervalMinutes = 10U;

    bool gridVisible = true;
    float gridSpacing = 1.0F;
    bool snapEnabled = false;
    float snapStep = 1.0F;
    float rotationSnapDegrees = 0.0F;

    bool assetBrowserRecursive = false;
    EditorAssetViewMode assetViewMode = EditorAssetViewMode::Tiles;
    EditorAssetSortMode assetSortMode = EditorAssetSortMode::Name;
    bool assetShowFolders = true;
    bool assetShowTemplates = true;
    float assetThumbnailScale = 1.0F;

    [[nodiscard]] bool operator==(const EditorWorkspacePreferences&) const noexcept = default;
};

struct EditorSettingsDocument {
    static constexpr std::uint32_t CurrentFileVersion = 1U;

    EditorRenderSettingsRecord renderer{};
    EditorWorkspacePreferences workspace{};

    [[nodiscard]] bool operator==(const EditorSettingsDocument&) const noexcept = default;
};

struct EditorSettingsLoadResult {
    EditorSettingsDocument settings{};
    bool found = false;
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept { return error.empty(); }
};

class EditorSettingsStore final {
public:
    static constexpr std::size_t MaximumBytes = 16U * 1024U;

    [[nodiscard]] static EditorSettingsLoadResult Load(const std::filesystem::path& path);
    [[nodiscard]] static bool Save(
        const std::filesystem::path& path,
        const EditorSettingsDocument& settings,
        std::string& error);
};

} // namespace kb::editor
