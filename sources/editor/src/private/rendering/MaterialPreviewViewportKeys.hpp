#pragma once

#include <cstdint>

namespace kb::editor {

inline constexpr std::uint64_t kInspectorMaterialPreviewViewportKey = 0x4D41545052455630ULL;
inline constexpr std::uint64_t kMaterialEditorPreviewViewportKey = 0x4D41545052455631ULL;
// Staging surface the thumbnail pipeline renders into while capturing a material.
inline constexpr std::uint64_t kMaterialThumbnailCaptureViewportKey = 0x4D41545052455632ULL;

} // namespace kb::editor
