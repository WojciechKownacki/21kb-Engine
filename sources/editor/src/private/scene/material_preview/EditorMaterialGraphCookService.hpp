#pragma once

#include "engine/assets/AssetId.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace kb::editor {

// Live status of a cook request for a single graph material asset (MAT-30/32/33).
enum class EditorMaterialGraphCookStatus : std::uint8_t {
    Idle,            // never requested
    Pending,         // queued, waiting out debounce window
    Cooking,         // codegen + shaderc running on the worker
    Ready,           // freshly compiled binaries staged on disk
    UpToDate,        // graph unchanged, every pass served from cache (no recompile)
    Stale,           // latest edit failed to cook, but a previous good program is still live
    Failed,          // codegen or shaderc failure with no last-good program to fall back on
    CookUnavailable, // shaderc tool could not be discovered
};

[[nodiscard]] std::string_view EditorMaterialGraphCookStatusName(EditorMaterialGraphCookStatus status) noexcept;

// Severity drives the banner colour in the material editor preview panel (MAT-32).
enum class EditorMaterialGraphCookBannerSeverity : std::uint8_t {
    None,    // nothing to show (no graph / never cooked)
    Pending, // compiling
    Ready,   // GPU graph program ready
    Warning, // fell back (cook unavailable / no GPU program)
    Error,   // compile/cook failure
};

struct EditorMaterialGraphCookBanner {
    std::string label;
    EditorMaterialGraphCookBannerSeverity severity = EditorMaterialGraphCookBannerSeverity::None;
};

// Map a cook status to the user-facing preview banner (MAT-32 status states).
[[nodiscard]] EditorMaterialGraphCookBanner MakeEditorMaterialGraphCookBanner(EditorMaterialGraphCookStatus status);

struct EditorMaterialGraphCookPassResult {
    std::string pass;
    bool succeeded = false;
    bool cacheHit = false;
    std::string binaryPath;
    std::uint64_t binaryByteSize = 0U;
};

struct EditorMaterialGraphCookResult {
    kb::assets::AssetId materialAssetId{};
    kb::render::RenderMaterialGraphVariantKey variantKey{};
    EditorMaterialGraphCookStatus status = EditorMaterialGraphCookStatus::Idle;
    std::uint64_t graphSourceHash = 0U;
    std::uint32_t materialTypeVersion = 1U;
    std::uint64_t requestGeneration = 0U;
    std::uint32_t elapsedMs = 0U;
    std::uint32_t compiledPassCount = 0U;
    std::uint32_t cacheHitPassCount = 0U;
    std::uint32_t cacheEntryCount = 0U;
    std::uint64_t cacheByteSize = 0U;
    std::uint32_t textureBindingCount = 0U;
    std::uint32_t uniformCount = 0U;
    std::uint32_t varyingCount = 0U;
    std::string backendName;
    bool budgetWarning = false;
    std::vector<EditorMaterialGraphCookPassResult> passes;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool HasGpuProgram() const noexcept {
        return status == EditorMaterialGraphCookStatus::Ready
            || status == EditorMaterialGraphCookStatus::UpToDate
            || status == EditorMaterialGraphCookStatus::Stale;
    }
};

struct EditorMaterialGraphCookConfig {
    std::string shadercPath;       // absolute path to bgfx shaderc; empty => CookUnavailable
    std::string varyingDefPath;    // varying.def.sc used by the mesh vertex shader
    std::vector<std::string> includeDirs; // graph shader include dirs (engine shaders + bgfx headers)
    std::vector<std::string> dependencyFiles; // explicit custom/generated includes outside the wrapper include graph
    std::string cacheRoot;         // per-project graph shader cache root (shared with the renderer)
    std::vector<std::string> passes; // graph passes to cook; defaults to BaseOpaque + GBuffer + ShadowDepth + BaseTransparent
    std::uint32_t materialTypeVersion = 1U;
    std::uint32_t debounceMs = 180U; // coalesce rapid edits into a single recook
    std::uint32_t compileWarningMs = 5000U;
    std::uint32_t cacheEntryWarningThreshold = 4096U;
    std::uint64_t cacheByteWarningThreshold = 256ULL * 1024ULL * 1024ULL;
    kb::render::RenderMaterialGraphShaderBackend backend = kb::render::RenderMaterialGraphShaderBackend::Dxbc;
    kb::assets::bake::ShaderBakePlatform shaderPlatform =
        kb::assets::bake::ShaderBakePlatform::Windows;

    // Resolve the cook backend that matches an active bgfx renderer so the cooked binary
    // lands in the directory the runtime program loader reads from.
    static kb::render::RenderMaterialGraphShaderBackend BackendForActiveRenderer() noexcept;

    // Build a config from compile-time defaults (shader source tree + prebuilt shaderc),
    // honoring the KB_GRAPH_SHADERC env override, targeting the active renderer backend
    // and the supplied per-project cache root.
    [[nodiscard]] static EditorMaterialGraphCookConfig Resolve(std::string cacheRoot);
};

// Cooks editor-authored material graphs into per-backend shaderc binaries that the renderer
// loads through MaterialProgramRegistry. Owns a single background worker that debounces and
// dedupes edits; CookNow() exposes the same deterministic executor synchronously for batch
// cooks (scene/project load) and tests.
class EditorMaterialGraphCookService {
public:
    EditorMaterialGraphCookService();
    explicit EditorMaterialGraphCookService(EditorMaterialGraphCookConfig config);
    ~EditorMaterialGraphCookService();

    EditorMaterialGraphCookService(const EditorMaterialGraphCookService&) = delete;
    EditorMaterialGraphCookService& operator=(const EditorMaterialGraphCookService&) = delete;

    [[nodiscard]] bool ShadercAvailable() const noexcept;
    [[nodiscard]] const EditorMaterialGraphCookConfig& Config() const noexcept { return config_; }
    [[nodiscard]] const std::string& CacheRoot() const noexcept { return config_.cacheRoot; }

    // Enqueue a debounced async cook of the working copy; supersedes any pending cook for the
    // same asset. Returns the request generation so callers can match completions.
    std::uint64_t RequestCook(
        kb::assets::AssetId assetId,
        const kb::render::RenderMaterialAssetData& material,
        kb::render::RenderMaterialGraphBuildContext graphContext = {});

    // Synchronous cook used by batch paths and tests. No debounce, no worker thread.
    [[nodiscard]] EditorMaterialGraphCookResult CookNow(
        kb::assets::AssetId assetId,
        const kb::render::RenderMaterialAssetData& material,
        kb::render::RenderMaterialGraphBuildContext graphContext = {}) const;

    // Drain completions published by the worker since the last call (UI poll, oldest first).
    [[nodiscard]] std::vector<EditorMaterialGraphCookResult> DrainResults();

    // Latest known result for an asset (Idle if never cooked).
    [[nodiscard]] EditorMaterialGraphCookResult LatestResult(kb::assets::AssetId assetId) const;
    [[nodiscard]] EditorMaterialGraphCookResult LatestResult(const kb::render::RenderMaterialGraphVariantKey& variantKey) const;
    [[nodiscard]] EditorMaterialGraphCookResult LatestResult(
        kb::assets::AssetId assetId,
        const kb::render::RenderMaterialGraphBuildContext& graphContext) const;

    // Block until no cook is pending or in flight (tests / save-before-build).
    void WaitForIdle();

private:
    struct PendingEntry {
        kb::render::RenderMaterialAssetData material;
        kb::render::RenderMaterialGraphBuildContext graphContext{};
        std::uint64_t generation = 0U;
        std::chrono::steady_clock::time_point readyAt{};
    };

    void StartWorker();
    void StopWorker();
    void WorkerLoop();

    EditorMaterialGraphCookConfig config_;

    mutable std::mutex mutex_;
    std::condition_variable wakeCv_;
    std::condition_variable idleCv_;
    using VariantMap = std::unordered_map<
        kb::render::RenderMaterialGraphVariantKey,
        EditorMaterialGraphCookResult,
        kb::render::RenderMaterialGraphVariantKeyHash>;
    std::unordered_map<
        kb::render::RenderMaterialGraphVariantKey,
        PendingEntry,
        kb::render::RenderMaterialGraphVariantKeyHash> pending_;
    VariantMap latest_;
    VariantMap lastGood_;
    std::vector<EditorMaterialGraphCookResult> completed_;
    std::uint64_t generationCounter_ = 0U;
    std::uint32_t inFlight_ = 0U;
    bool stop_ = false;
    std::thread worker_;
};

} // namespace kb::editor
