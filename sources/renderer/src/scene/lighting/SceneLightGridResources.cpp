#include "kb/render/scene/SceneLightGridResources.hpp"
#include "scene/lighting/SceneLightGrid.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace kb::render {

struct SceneLightGridResources::State {
    struct Key {
        std::uint64_t revision = 0U, primary = 0U;
        std::uint32_t mask = 0U;
        std::array<std::uint16_t, 3> dimensions{};
        std::optional<ScenePackedLight> preview;
        bool operator==(const Key&) const noexcept = default;
    };
    struct Slot {
        Key key{};
        SceneLightGridBinding binding{};
        std::uint64_t lastUsed = 0U;
        std::uint16_t height = 0U;
    };
    SceneLightGrid grid;
    std::vector<Slot> slots;
    std::uint64_t frame = 1U;
    bgfx::TextureHandle fallback = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
    std::array<bgfx::UniformHandle, 4> uniforms{{
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
};

SceneLightGridResources::SceneLightGridResources() : state_(std::make_unique<State>()) {}
SceneLightGridResources::~SceneLightGridResources() { Shutdown(); }

bool SceneLightGridResources::Initialize() {
    if (bgfx::isValid(state_->sampler)) return true;
    state_->sampler = bgfx::createUniform("s_sceneLightGrid", bgfx::UniformType::Sampler);
    const std::uint32_t black = 0U;
    state_->fallback = bgfx::createTexture2D(1U, 1U, false, 1U,
        bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_POINT, bgfx::copy(&black, sizeof(black)));
    constexpr std::array names{"u_sceneLightGridMinimum", "u_sceneLightGridInverseCellSize",
        "u_sceneLightGridDimensions", "u_sceneLightGridLayout"};
    for (std::size_t index = 0U; index < names.size(); ++index)
        state_->uniforms[index] = bgfx::createUniform(names[index], bgfx::UniformType::Vec4);
    if (bgfx::isValid(state_->sampler) && bgfx::isValid(state_->fallback) && std::ranges::all_of(state_->uniforms,
            [](auto handle) { return bgfx::isValid(handle); })) return true;
    Shutdown();
    return false;
}

void SceneLightGridResources::Shutdown() noexcept {
    for (const auto& slot : state_->slots)
        if (bgfx::isValid(slot.binding.texture)) bgfx::destroy(slot.binding.texture);
    state_->slots.clear();
    for (auto& uniform : state_->uniforms) {
        if (bgfx::isValid(uniform)) bgfx::destroy(uniform);
        uniform = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(state_->sampler)) bgfx::destroy(state_->sampler);
    state_->sampler = BGFX_INVALID_HANDLE;
    if (bgfx::isValid(state_->fallback)) bgfx::destroy(state_->fallback);
    state_->fallback = BGFX_INVALID_HANDLE;
    state_->frame = 1U;
}

SceneLightGridBinding SceneLightGridResources::Prepare(const RenderScene& scene,
    SceneRenderLightingConfig config, std::uint32_t cameraMask, std::uint64_t primaryLightId,
    SceneRenderSubmitStats& stats, SceneRenderDiagnostics* diagnostics) {
    const auto unavailable = [&]() {
        stats.lightingPathProduction = false;
        stats.lightClusterCount = 0U;
        if (diagnostics != nullptr) diagnostics->events.push_back({
            .severity = SceneRenderDiagnosticSeverity::Error,
            .kind = SceneRenderDiagnosticKind::LightGridUnavailable});
        return SceneLightGridBinding{};
    };
    const auto publish = [&](const SceneLightGridBinding& binding) {
        stats.submittedForwardLightCount = stats.forwardLightCapacity = binding.lightCount;
        stats.skippedForwardLightCount = 0U;
        stats.lightClusterCount = static_cast<std::uint32_t>(binding.dimensions[0] *
            binding.dimensions[1] * binding.dimensions[2]);
        return binding;
    };
    if (!bgfx::isValid(state_->sampler)) return unavailable();
    const State::Key key{scene.LightContentRevision(), primaryLightId, cameraMask,
        config.clusterDimensions, SceneLightShaderData::EditorPreview(config)};
    for (auto& slot : state_->slots) {
        if (slot.key == key && bgfx::isValid(slot.binding.texture)) {
            slot.lastUsed = state_->frame;
            return publish(slot.binding);
        }
    }
    const auto* caps = bgfx::getCaps();
    if (caps == nullptr || !(caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_2D))
        return unavailable();
    const auto width = static_cast<std::uint16_t>(std::min<std::uint32_t>(256U, caps->limits.maxTextureSize));
    if (width == 0U) return unavailable();
    const auto maxTexels = std::min<std::uint32_t>(1U << 24U, width * caps->limits.maxTextureSize);
    if (!state_->grid.Build(scene.LightProxies(), config, cameraMask, primaryLightId, maxTexels))
        return unavailable();
    // A resource already used in this frame cannot be overwritten: bgfx applies
    // texture updates before drawing the frame's views, including earlier submits.
    auto found = std::ranges::find_if(state_->slots, [&](const auto& slot) {
        return slot.lastUsed < state_->frame;
    });
    if (found == state_->slots.end()) {
        state_->slots.emplace_back();
        found = std::prev(state_->slots.end());
    }
    auto& slot = *found;
    const auto rows = static_cast<std::uint32_t>((state_->grid.Atlas().size() + width - 1U) / width);
    if (rows > slot.height || !bgfx::isValid(slot.binding.texture)) {
        if (bgfx::isValid(slot.binding.texture)) bgfx::destroy(slot.binding.texture);
        slot.binding.texture = BGFX_INVALID_HANDLE;
        slot.height = static_cast<std::uint16_t>(rows);
        slot.binding.texture = bgfx::createTexture2D(width, slot.height, false, 1U,
            bgfx::TextureFormat::RGBA32F, BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(slot.binding.texture)) return unavailable();
    }
    const auto bytes = rows * width * 16U;
    const auto* memory = bgfx::alloc(bytes);
    std::memset(memory->data, 0, bytes);
    std::memcpy(memory->data, state_->grid.Atlas().data(), state_->grid.Atlas().size_bytes());
    bgfx::updateTexture2D(slot.binding.texture, 0U, 0U, 0U, 0U, width,
        static_cast<std::uint16_t>(rows), memory);
    slot.key = key;
    slot.lastUsed = state_->frame;
    slot.binding.minimum = state_->grid.Minimum();
    slot.binding.inverseCellSize = state_->grid.InverseCellSize();
    slot.binding.dimensions = state_->grid.Dimensions();
    slot.binding.layout = {static_cast<float>(state_->grid.HeaderOffset()),
        static_cast<float>(state_->grid.IndexOffset()), 1.0F / width, 1.0F / slot.height};
    slot.binding.lightCount = state_->grid.LightCount();
    return publish(slot.binding);
}

void SceneLightGridResources::Bind(const SceneLightGridBinding& binding, std::uint8_t stage) const noexcept {
    const std::array<const float*, 4> values{binding.minimum.data(), binding.inverseCellSize.data(),
        binding.dimensions.data(), binding.layout.data()};
    for (std::size_t index = 0U; index < values.size(); ++index)
        if (bgfx::isValid(state_->uniforms[index])) bgfx::setUniform(state_->uniforms[index], values[index]);
    if (bgfx::isValid(state_->sampler))
        bgfx::setTexture(stage, state_->sampler, bgfx::isValid(binding.texture) ? binding.texture : state_->fallback);
}

void SceneLightGridResources::EndFrame() noexcept {
    ++state_->frame;
    std::erase_if(state_->slots, [&](const auto& slot) {
        if (slot.lastUsed + 3U >= state_->frame) return false;
        if (bgfx::isValid(slot.binding.texture)) bgfx::destroy(slot.binding.texture);
        return true;
    });
}

} // namespace kb::render
