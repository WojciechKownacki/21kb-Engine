#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace kb::render {

// Shader stage recovered from a bgfx shader blob header.
enum class PrewarmShaderStage {
    Vertex,
    Fragment,
    Compute,
};

// A raw D3D bytecode span extracted from a bgfx shader blob (the DXBC payload for the Direct3D11
// backend). `code`/`size` point *into* the caller-owned blob buffer; they are only valid while that
// buffer lives.
struct PrewarmShaderBytecode {
    PrewarmShaderStage stage = PrewarmShaderStage::Fragment;
    const void* code = nullptr;
    std::uint32_t size = 0U;
};

// Pure parser: walk a bgfx compiled-shader blob (magic "VSH"/"FSH"/"CSH" + version, uniform table,
// then the backend bytecode) and return the embedded bytecode span. Mirrors ShaderD3D11::create in
// third_party/bgfx so the offset math stays in lock-step with the blob format the renderer actually
// consumes. Returns nullopt on any malformed / truncated / non-shader input. No graphics dependency,
// so it is unit-testable headless.
[[nodiscard]] std::optional<PrewarmShaderBytecode> ExtractBgfxShaderBytecode(std::span<const std::uint8_t> blob) noexcept;

// Best-effort driver shader-cache warm. When the active bgfx backend is Direct3D11, parse `blob`,
// grab the live ID3D11Device (bgfx::getInternalData) and create+release the matching pixel/vertex
// shader so the driver compiles the DXBC->ISA *now*, on the calling thread, and caches the result by
// bytecode. A later bgfx::createShader of the same bytecode on the render thread then hits that cache
// instead of stalling bgfx::frame() for the full compile. Deduplicated by bytecode hash, so repeated
// calls for an already-warmed shader are cheap no-ops. Safe to call from any thread (ID3D11Device
// resource creation is free-threaded). No-op (returns false) on any other backend or on parse failure.
// Returns true iff a warm was actually issued this call.
bool PrewarmGraphShaderBlob(std::span<const std::uint8_t> blob) noexcept;

// Convenience wrapper: read a cooked .bin file from disk and warm it. Returns true iff warmed.
bool PrewarmGraphShaderFile(const std::filesystem::path& path) noexcept;

// Total number of shaders actually warmed since process start (test/telemetry hook).
[[nodiscard]] std::uint64_t GraphShaderPrewarmCount() noexcept;

// Clears the dedup set so a subsequent identical blob warms again (tests only).
void ResetGraphShaderPrewarmStateForTesting() noexcept;

} // namespace kb::render
