#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kb::editor {

inline constexpr int kEditorMeshThumbnailSize = 128;
inline constexpr int kEditorMeshPreviewSize = 512;

enum class EditorMeshPreviewRenderMode : std::uint8_t {
    Solid,
    WireframeOnly,
    WireframeOverlay,
    Normals,
    Bounds,
};

enum class EditorMeshPreviewLightPreset : std::uint8_t {
    Studio,
    Front,
    Rim,
};

struct EditorMeshThumbnailImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> bgra;
};

struct EditorMeshThumbnailStats {
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t triangleCount = 0;
    std::uint32_t materialSlotCount = 0;
    float boundsCenter[3]{};
    float boundsRadius = 0.0F;
};

struct EditorMeshPreviewSettings {
    float yawDegrees = -35.0F;
    float pitchDegrees = 24.0F;
    float zoom = 1.0F;
    EditorMeshPreviewRenderMode renderMode = EditorMeshPreviewRenderMode::Solid;
    EditorMeshPreviewLightPreset lightPreset = EditorMeshPreviewLightPreset::Studio;

    [[nodiscard]] bool operator==(const EditorMeshPreviewSettings& other) const noexcept {
        return yawDegrees == other.yawDegrees
            && pitchDegrees == other.pitchDegrees
            && zoom == other.zoom
            && renderMode == other.renderMode
            && lightPreset == other.lightPreset;
    }
};

struct EditorMeshPreviewVector3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct EditorMeshPreviewGeometry {
    EditorMeshThumbnailStats stats{};
    std::vector<EditorMeshPreviewVector3> positions{};
    std::vector<EditorMeshPreviewVector3> normals{};
    std::vector<std::uint32_t> indices{};
};

enum class EditorMeshValidationSeverity : std::uint8_t {
    Info,
    Warning,
    Error,
};

struct EditorMeshValidationIssue {
    EditorMeshValidationSeverity severity = EditorMeshValidationSeverity::Info;
    std::string message{};
};

struct EditorMeshValidationResult {
    std::vector<EditorMeshValidationIssue> issues{};

    [[nodiscard]] bool HasErrors() const noexcept {
        for (const EditorMeshValidationIssue& issue : issues) {
            if (issue.severity == EditorMeshValidationSeverity::Error) {
                return true;
            }
        }
        return false;
    }
};

} // namespace kb::editor
