#pragma once

#include "kb/render/effects/RenderEffectApi.hpp"
#include <filesystem>
#include <memory>
#include <string>

namespace kb::render {
struct RenderEffectStats {
    std::uint32_t plugins = 0U, programs = 0U, buffers = 0U, textures = 0U, uniforms = 0U, readbacks = 0U;
    std::uint64_t dispatches = 0U, presentations = 0U, completedReadbacks = 0U;
};
// Renderer-thread-only. Load/unload between Run calls. Native modules use the
// same verified-release loader as engine modules; native code remains trusted.
class RenderEffectHost {
public:
    RenderEffectHost();
    ~RenderEffectHost();
    RenderEffectHost(const RenderEffectHost&) = delete;
    RenderEffectHost& operator=(const RenderEffectHost&) = delete;
    [[nodiscard]] bool Load(const std::filesystem::path& manifest, std::string& error);
    void Unload() noexcept;
    // Graphics handles belong to the host application, never to the plugin ABI.
    [[nodiscard]] bool Run(std::uint16_t firstView, std::uint32_t width, std::uint32_t height,
        std::uint32_t completedFrame, float deltaSeconds, std::uint16_t framebuffer = UINT16_MAX,
        std::uint16_t sceneColor = UINT16_MAX);
    [[nodiscard]] RenderEffectStats Stats() const noexcept;
    [[nodiscard]] std::uint64_t PluginStatus(std::uint32_t index) const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Export compiled native code and shader variants into an independent directory.
// The output must not exist. Publication uses a staging directory and rename.
[[nodiscard]] bool ExportRenderEffect(const std::filesystem::path& manifest,
    const std::filesystem::path& output, std::string& error);
} // namespace kb::render
