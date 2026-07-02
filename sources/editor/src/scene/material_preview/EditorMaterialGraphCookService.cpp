#include "scene/material_preview/EditorMaterialGraphCookService.hpp"

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kb::editor {
namespace {

[[nodiscard]] std::filesystem::path ExecutableDirectory() {
#if defined(_WIN32)
    std::wstring buffer(1024U, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U || length >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path{ buffer }.parent_path();
#else
    return {};
#endif
}

// Locate bgfx's shaderc relative to the running editor so a normal build cooks graphs out of the box,
// without requiring the KB_GRAPH_SHADERC environment variable to be set by hand.
[[nodiscard]] std::string DiscoverShadercNearExecutable() {
    std::filesystem::path dir = ExecutableDirectory();
    if (dir.empty()) {
        return {};
    }
    static constexpr std::array<const char*, 4U> kConfigs{ "Debug", "Release", "RelWithDebInfo", "MinSizeRel" };
    std::error_code error;
    for (int level = 0; level < 8 && !dir.empty(); ++level) {
        for (const char* exe : { "shaderc.exe", "shaderc" }) {
            const std::filesystem::path candidate = dir / exe;
            if (std::filesystem::exists(candidate, error)) {
                return candidate.string();
            }
        }
        for (const char* config : kConfigs) {
            const std::filesystem::path candidate =
                dir / "third_party" / "bgfx.cmake" / "cmake" / "bgfx" / config / "shaderc.exe";
            if (std::filesystem::exists(candidate, error)) {
                return candidate.string();
            }
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return {};
}

// Find the engine source root (the directory containing sources/renderer/shaders/varying.def.sc) by
// walking up from the running editor, so the graph cook has the varying def + shader include dirs even
// when the compile-time paths were not baked in.
[[nodiscard]] std::filesystem::path DiscoverEngineShaderRoot() {
    std::filesystem::path dir = ExecutableDirectory();
    if (dir.empty()) {
        return {};
    }
    std::error_code error;
    for (int level = 0; level < 10 && !dir.empty(); ++level) {
        if (std::filesystem::exists(dir / "sources" / "renderer" / "shaders" / "varying.def.sc", error)) {
            return dir;
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return {};
}

[[nodiscard]] std::string ReadEnv(const char* name) {
#if defined(_WIN32)
    char* buffer = nullptr;
    std::size_t size = 0U;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
        return {};
    }
    std::string value{ buffer };
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string{ value } : std::string{};
#endif
}

[[nodiscard]] std::string SeverityTag(kb::render::RenderMaterialGraphDiagnosticSeverity severity) {
    return severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error ? "error" : "warning";
}

[[nodiscard]] std::string FormatDiagnostic(const kb::render::RenderMaterialGraphDiagnostic& diagnostic) {
    std::string text = "[" + SeverityTag(diagnostic.severity) + "]";
    if (diagnostic.nodeId != 0U) {
        text += " node=" + std::to_string(diagnostic.nodeId);
    }
    if (!diagnostic.pin.empty()) {
        text += " pin=" + diagnostic.pin;
    }
    if (!diagnostic.pass.empty()) {
        text += " pass=" + diagnostic.pass;
    }
    if (!diagnostic.backend.empty()) {
        text += " backend=" + diagnostic.backend;
    }
    text += ": " + diagnostic.message;
    return text;
}

[[nodiscard]] std::vector<std::string> EffectivePasses(const EditorMaterialGraphCookConfig& config) {
    if (!config.passes.empty()) {
        return config.passes;
    }
    // BaseOpaque (MAT-08 GPU forward), ShadowDepth (graph alpha clip) and BaseTransparent (MAT-80, now an
    // active alpha-blended submit pass) all have a live runtime consumer.
    return { "BaseOpaque", "ShadowDepth", "BaseTransparent" };
}

struct CookCacheFootprint {
    std::uint32_t entryCount = 0U;
    std::uint64_t byteSize = 0U;
};

[[nodiscard]] CookCacheFootprint MeasureCookCacheFootprint(std::string_view cacheRoot) {
    CookCacheFootprint footprint{};
    if (cacheRoot.empty()) {
        return footprint;
    }
    std::error_code error;
    const std::filesystem::path root{ std::string{ cacheRoot } };
    if (!std::filesystem::exists(root, error)) {
        return footprint;
    }
    for (std::filesystem::recursive_directory_iterator it{ root, error }, end; it != end && !error; it.increment(error)) {
        if (!it->is_regular_file(error)) {
            continue;
        }
        if (footprint.entryCount < std::numeric_limits<std::uint32_t>::max()) {
            ++footprint.entryCount;
        }
        const std::uintmax_t fileSize = it->file_size(error);
        if (!error) {
            footprint.byteSize += fileSize;
        }
        error.clear();
    }
    return footprint;
}

void AppendCookBudgetWarnings(const EditorMaterialGraphCookConfig& config, EditorMaterialGraphCookResult& result) {
    const auto warn = [&result](std::string message) {
        result.budgetWarning = true;
        result.diagnostics.push_back("[warning]: graph cook budget exceeded - " + std::move(message));
    };
    if (result.elapsedMs > config.compileWarningMs) {
        warn("cook time " + std::to_string(result.elapsedMs) + "ms/" + std::to_string(config.compileWarningMs) + "ms");
    }
    if (result.cacheEntryCount > config.cacheEntryWarningThreshold) {
        warn("cache entries " + std::to_string(result.cacheEntryCount) + "/" + std::to_string(config.cacheEntryWarningThreshold));
    }
    if (result.cacheByteSize > config.cacheByteWarningThreshold) {
        warn("cache bytes " + std::to_string(result.cacheByteSize) + "/" + std::to_string(config.cacheByteWarningThreshold));
    }
}

// Deterministic, side-effect-free cook of a single material graph against an explicit config.
// Shared by the synchronous CookNow() and the background worker (which snapshots the config under
// the service lock) so neither path races on mutable service state.
[[nodiscard]] EditorMaterialGraphCookResult ExecuteCook(
    const EditorMaterialGraphCookConfig& config,
    kb::assets::AssetId assetId,
    const kb::render::RenderMaterialAssetData& material,
    kb::render::RenderMaterialGraphBuildContext graphContext) {
    const auto startedAt = std::chrono::steady_clock::now();
    EditorMaterialGraphCookResult result{};
    result.materialAssetId = assetId;
    result.materialTypeVersion = material.materialTypeVersion;

    const auto finish = [&config, startedAt](EditorMaterialGraphCookResult& cookResult) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt).count();
        cookResult.elapsedMs = elapsed <= 0
            ? 0U
            : static_cast<std::uint32_t>(std::min<std::int64_t>(elapsed, std::numeric_limits<std::uint32_t>::max()));
        const CookCacheFootprint footprint = MeasureCookCacheFootprint(config.cacheRoot);
        cookResult.cacheEntryCount = footprint.entryCount;
        cookResult.cacheByteSize = footprint.byteSize;
        AppendCookBudgetWarnings(config, cookResult);
    };

    if (config.shadercPath.empty()) {
        result.status = EditorMaterialGraphCookStatus::CookUnavailable;
        result.diagnostics.emplace_back("[error]: graph shader cook unavailable - shaderc tool not found (set KB_GRAPH_SHADERC or build bgfx::shaderc)");
        finish(result);
        return result;
    }

    graphContext.assetId = assetId.value;
    const kb::render::RenderMaterialGraphCompileResult compiled =
        kb::render::CompileRenderMaterialGraphToShaderSource(material.graph, graphContext);
    for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : compiled.diagnostics) {
        result.diagnostics.push_back(FormatDiagnostic(diagnostic));
    }
    if (!compiled.Succeeded()) {
        result.status = EditorMaterialGraphCookStatus::Failed;
        finish(result);
        return result;
    }
    result.graphSourceHash = compiled.shader.sourceHash;

    const std::array<kb::render::RenderMaterialGraphShaderBackend, 1U> backends{ config.backend };
    bool anyFailure = false;
    bool allCacheHit = true;
    for (const std::string& pass : EffectivePasses(config)) {
        kb::render::RenderMaterialGraphShaderArtifactRequest request{};
        request.shadercPath = config.shadercPath;
        request.varyingDefPath = config.varyingDefPath;
        request.includeDirs = config.includeDirs;
        request.cacheRoot = config.cacheRoot;
        request.pass = pass;
        request.materialTypeVersion = material.materialTypeVersion;

        const kb::render::RenderMaterialGraphShaderArtifactResult cook =
            kb::render::CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, request);
        for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : cook.diagnostics) {
            result.diagnostics.push_back(FormatDiagnostic(diagnostic));
        }

        EditorMaterialGraphCookPassResult passResult{};
        passResult.pass = pass;
        const kb::render::RenderMaterialGraphShaderBinary* binary =
            cook.Succeeded() && cook.artifact.has_value() ? cook.artifact->FindBinary(config.backend) : nullptr;
        if (binary != nullptr) {
            passResult.succeeded = true;
            passResult.cacheHit = binary->cacheHit;
            passResult.binaryPath = binary->binaryPath;
            passResult.binaryByteSize = binary->byteSize;
            if (binary->cacheHit) {
                ++result.cacheHitPassCount;
            } else {
                ++result.compiledPassCount;
            }
            allCacheHit = allCacheHit && binary->cacheHit;
        } else {
            anyFailure = true;
            allCacheHit = false;
        }
        result.passes.push_back(std::move(passResult));
    }

    result.status = anyFailure
        ? EditorMaterialGraphCookStatus::Failed
        : (allCacheHit ? EditorMaterialGraphCookStatus::UpToDate : EditorMaterialGraphCookStatus::Ready);
    finish(result);
    return result;
}

} // namespace

std::string_view EditorMaterialGraphCookStatusName(EditorMaterialGraphCookStatus status) noexcept {
    switch (status) {
    case EditorMaterialGraphCookStatus::Idle:
        return "Idle";
    case EditorMaterialGraphCookStatus::Pending:
        return "Pending";
    case EditorMaterialGraphCookStatus::Cooking:
        return "Cooking";
    case EditorMaterialGraphCookStatus::Ready:
        return "Ready";
    case EditorMaterialGraphCookStatus::UpToDate:
        return "UpToDate";
    case EditorMaterialGraphCookStatus::Stale:
        return "Stale";
    case EditorMaterialGraphCookStatus::Failed:
        return "Failed";
    case EditorMaterialGraphCookStatus::CookUnavailable:
        return "CookUnavailable";
    }
    return "Idle";
}

EditorMaterialGraphCookBanner MakeEditorMaterialGraphCookBanner(EditorMaterialGraphCookStatus status) {
    switch (status) {
    case EditorMaterialGraphCookStatus::Pending:
    case EditorMaterialGraphCookStatus::Cooking:
        return { "Compiling graph shader...", EditorMaterialGraphCookBannerSeverity::Pending };
    case EditorMaterialGraphCookStatus::Ready:
    case EditorMaterialGraphCookStatus::UpToDate:
        return { "Ready (GPU graph)", EditorMaterialGraphCookBannerSeverity::Ready };
    case EditorMaterialGraphCookStatus::Stale:
        return { "Stale (last-good) - fix graph to refresh", EditorMaterialGraphCookBannerSeverity::Warning };
    case EditorMaterialGraphCookStatus::Failed:
        return { "Failed (error material) - see diagnostics", EditorMaterialGraphCookBannerSeverity::Error };
    case EditorMaterialGraphCookStatus::CookUnavailable:
        return { "Fallback (CPU/builtin) - shaderc unavailable", EditorMaterialGraphCookBannerSeverity::Warning };
    case EditorMaterialGraphCookStatus::Idle:
        break;
    }
    return { {}, EditorMaterialGraphCookBannerSeverity::None };
}

kb::render::RenderMaterialGraphShaderBackend EditorMaterialGraphCookConfig::BackendForActiveRenderer() noexcept {
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::Direct3D12:
        return kb::render::RenderMaterialGraphShaderBackend::Dxil;
    case bgfx::RendererType::Vulkan:
        return kb::render::RenderMaterialGraphShaderBackend::Spirv;
    case bgfx::RendererType::OpenGL:
        return kb::render::RenderMaterialGraphShaderBackend::Glsl;
    case bgfx::RendererType::OpenGLES:
        return kb::render::RenderMaterialGraphShaderBackend::Essl;
    case bgfx::RendererType::Metal:
        return kb::render::RenderMaterialGraphShaderBackend::Metal;
    case bgfx::RendererType::Noop:
    case bgfx::RendererType::Direct3D11:
    default:
        return kb::render::RenderMaterialGraphShaderBackend::Dxbc;
    }
}

EditorMaterialGraphCookConfig EditorMaterialGraphCookConfig::Resolve(std::string cacheRoot) {
    EditorMaterialGraphCookConfig config{};
    config.cacheRoot = std::move(cacheRoot);
    config.backend = BackendForActiveRenderer();

    // KB_GRAPH_SHADERC env override takes precedence so a non-default toolchain can be used
    // without rebuilding the editor; otherwise fall back to the compile-time prebuilt path.
    std::string shaderc = ReadEnv("KB_GRAPH_SHADERC");
#if defined(KB_EDITOR_GRAPH_SHADERC_PATH)
    if (shaderc.empty()) {
        shaderc = KB_EDITOR_GRAPH_SHADERC_PATH;
    }
#endif
    std::error_code shadercError;
    if (shaderc.empty() || !std::filesystem::exists(shaderc, shadercError)) {
        // Neither the env override nor the compile-time path resolved; discover shaderc next to the editor.
        shaderc = DiscoverShadercNearExecutable();
    }
    if (!shaderc.empty() && std::filesystem::exists(shaderc, shadercError)) {
        config.shadercPath = std::move(shaderc);
    }

#if defined(KB_EDITOR_GRAPH_SHADER_VARYING_DEF)
    config.varyingDefPath = KB_EDITOR_GRAPH_SHADER_VARYING_DEF;
#endif
#if defined(KB_EDITOR_GRAPH_SHADER_INCLUDE_DIR)
    config.includeDirs.emplace_back(KB_EDITOR_GRAPH_SHADER_INCLUDE_DIR);
#endif
#if defined(KB_EDITOR_GRAPH_BGFX_SHADER_INCLUDE_DIR)
    config.includeDirs.emplace_back(KB_EDITOR_GRAPH_BGFX_SHADER_INCLUDE_DIR);
#endif

    // If the compile-time shader-source paths were not baked in, discover them next to the editor so the
    // cook has a valid varying def and the graph/bgfx shader include directories.
    std::error_code sourceError;
    if (config.varyingDefPath.empty() || !std::filesystem::exists(config.varyingDefPath, sourceError)) {
        const std::filesystem::path engineRoot = DiscoverEngineShaderRoot();
        if (!engineRoot.empty()) {
            config.varyingDefPath = (engineRoot / "sources" / "renderer" / "shaders" / "varying.def.sc").string();
            config.includeDirs.clear();
            config.includeDirs.emplace_back((engineRoot / "sources" / "renderer" / "shaders").string());
            config.includeDirs.emplace_back((engineRoot / "third_party" / "bgfx.cmake" / "bgfx" / "src").string());
        }
    }
    return config;
}

EditorMaterialGraphCookService::EditorMaterialGraphCookService()
    : EditorMaterialGraphCookService(EditorMaterialGraphCookConfig{}) {
}

EditorMaterialGraphCookService::EditorMaterialGraphCookService(EditorMaterialGraphCookConfig config)
    : config_(std::move(config)) {
    StartWorker();
}

EditorMaterialGraphCookService::~EditorMaterialGraphCookService() {
    StopWorker();
}

bool EditorMaterialGraphCookService::ShadercAvailable() const noexcept {
    return !config_.shadercPath.empty();
}

std::uint64_t EditorMaterialGraphCookService::RequestCook(
    kb::assets::AssetId assetId,
    const kb::render::RenderMaterialAssetData& material,
    kb::render::RenderMaterialGraphBuildContext graphContext) {
    std::uint64_t generation = 0U;
    {
        std::lock_guard<std::mutex> lock{ mutex_ };
        // Track the live renderer backend so cooked binaries land in the directory the runtime
        // program loader reads from for the active bgfx renderer.
        config_.backend = EditorMaterialGraphCookConfig::BackendForActiveRenderer();
        generation = ++generationCounter_;
        PendingEntry entry{};
        entry.material = material;
        entry.graphContext = std::move(graphContext);
        entry.generation = generation;
        entry.readyAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.debounceMs);
        pending_[assetId.value] = std::move(entry);

        EditorMaterialGraphCookResult pendingResult{};
        pendingResult.materialAssetId = assetId;
        pendingResult.materialTypeVersion = material.materialTypeVersion;
        pendingResult.requestGeneration = generation;
        pendingResult.status = config_.shadercPath.empty()
            ? EditorMaterialGraphCookStatus::CookUnavailable
            : EditorMaterialGraphCookStatus::Pending;
        latest_[assetId.value] = std::move(pendingResult);
    }
    wakeCv_.notify_all();
    return generation;
}

EditorMaterialGraphCookResult EditorMaterialGraphCookService::CookNow(
    kb::assets::AssetId assetId,
    const kb::render::RenderMaterialAssetData& material,
    kb::render::RenderMaterialGraphBuildContext graphContext) const {
    EditorMaterialGraphCookConfig snapshot;
    {
        std::lock_guard<std::mutex> lock{ mutex_ };
        snapshot = config_;
    }
    return ExecuteCook(snapshot, assetId, material, std::move(graphContext));
}

std::vector<EditorMaterialGraphCookResult> EditorMaterialGraphCookService::DrainResults() {
    std::lock_guard<std::mutex> lock{ mutex_ };
    std::vector<EditorMaterialGraphCookResult> drained;
    drained.swap(completed_);
    return drained;
}

EditorMaterialGraphCookResult EditorMaterialGraphCookService::LatestResult(kb::assets::AssetId assetId) const {
    std::lock_guard<std::mutex> lock{ mutex_ };
    const auto it = latest_.find(assetId.value);
    if (it != latest_.end()) {
        return it->second;
    }
    EditorMaterialGraphCookResult idle{};
    idle.materialAssetId = assetId;
    idle.status = EditorMaterialGraphCookStatus::Idle;
    return idle;
}

void EditorMaterialGraphCookService::WaitForIdle() {
    std::unique_lock<std::mutex> lock{ mutex_ };
    idleCv_.wait(lock, [this] { return pending_.empty() && inFlight_ == 0U; });
}

void EditorMaterialGraphCookService::StartWorker() {
    worker_ = std::thread{ [this] { WorkerLoop(); } };
}

void EditorMaterialGraphCookService::StopWorker() {
    {
        std::lock_guard<std::mutex> lock{ mutex_ };
        stop_ = true;
        pending_.clear();
    }
    wakeCv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void EditorMaterialGraphCookService::WorkerLoop() {
    std::unique_lock<std::mutex> lock{ mutex_ };
    while (true) {
        wakeCv_.wait(lock, [this] { return stop_ || !pending_.empty(); });
        if (stop_) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        auto dueIt = pending_.end();
        auto earliest = std::chrono::steady_clock::time_point::max();
        for (auto it = pending_.begin(); it != pending_.end(); ++it) {
            if (it->second.readyAt <= now) {
                dueIt = it;
                break;
            }
            earliest = std::min(earliest, it->second.readyAt);
        }
        if (dueIt == pending_.end()) {
            wakeCv_.wait_until(lock, earliest);
            continue;
        }

        const std::uint64_t assetKey = dueIt->first;
        PendingEntry entry = std::move(dueIt->second);
        pending_.erase(dueIt);
        ++inFlight_;

        const kb::assets::AssetId assetId{ assetKey };
        latest_[assetKey].status = EditorMaterialGraphCookStatus::Cooking;
        const EditorMaterialGraphCookConfig snapshot = config_;

        lock.unlock();
        EditorMaterialGraphCookResult result = ExecuteCook(snapshot, assetId, entry.material, std::move(entry.graphContext));
        result.requestGeneration = entry.generation;
        lock.lock();

        const auto superseded = pending_.find(assetKey);
        const bool stillCurrent = superseded == pending_.end() || superseded->second.generation <= entry.generation;
        if (stillCurrent) {
            // Hot-reload last-good policy (MAT-33): a successful cook becomes the new last-good; a
            // failed/unavailable cook keeps the previous good program live and reports Stale so the
            // viewport never drops to a black/error frame while the artist is mid-edit.
            if (result.status == EditorMaterialGraphCookStatus::Ready || result.status == EditorMaterialGraphCookStatus::UpToDate) {
                lastGood_[assetKey] = result;
            } else if (result.status == EditorMaterialGraphCookStatus::Failed || result.status == EditorMaterialGraphCookStatus::CookUnavailable) {
                const auto good = lastGood_.find(assetKey);
                if (good != lastGood_.end()) {
                    EditorMaterialGraphCookResult stale = good->second;
                    stale.status = EditorMaterialGraphCookStatus::Stale;
                    stale.requestGeneration = result.requestGeneration;
                    stale.diagnostics = result.diagnostics; // carry the failure reason for the panel
                    result = std::move(stale);
                }
            }
            latest_[assetKey] = result;
            completed_.push_back(std::move(result));
        }
        --inFlight_;
        idleCv_.notify_all();
    }
}

} // namespace kb::editor
