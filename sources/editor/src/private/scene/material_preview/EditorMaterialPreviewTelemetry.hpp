#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::editor {

enum class MaterialPreviewRenderMode : std::uint8_t {
    BuiltinPbr,
    CpuPbrFlatteningFallback,
    GpuMaterialGraph,
    ErrorMaterial,
    LastGood,
};

struct EditorMaterialPreviewTelemetry {
    kb::assets::AssetId materialAssetId{};
    bool materialMetadataFound = false;
    bool materialLoaded = false;
    bool previewSceneReady = false;
    std::uint32_t missingTextureCount = 0;
    std::vector<std::string> missingTextures;
    MaterialPreviewRenderMode renderMode = MaterialPreviewRenderMode::BuiltinPbr;
    bool graphBacked = false;
    std::uint64_t graphProgramKey = 0;
    kb::render::RenderMaterialGraphRuntimeState graphRuntimeState = kb::render::RenderMaterialGraphRuntimeState::UsingGpuGraph;
    std::vector<std::string> compileDiagnostics;
};

class EditorMaterialPreviewTelemetryBuilder {
public:
    EditorMaterialPreviewTelemetryBuilder() = delete;

    [[nodiscard]] static EditorMaterialPreviewTelemetry Build(
        const kb::assets::AssetManager& manager,
        kb::assets::AssetId materialAssetId,
        const kb::render::RenderMaterialAssetData* material,
        bool previewSceneReady,
        kb::render::RenderMaterialGraphBuildContext graphContext = {},
        // Whether the GPU graph toolchain can actually produce the cooked program
        // binary (i.e. shaderc is available). When false, a graph that compiles to
        // shader SOURCE still cannot run on the GPU, so the scene renders the CPU
        // PBR flatten — the telemetry must report that instead of claiming
        // GpuMaterialGraph (Finding 3: no more "GPU graph" telemetry with no shaderc).
        bool gpuProgramAvailable = true);
};

} // namespace kb::editor
