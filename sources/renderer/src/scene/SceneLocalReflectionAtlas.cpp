#include "kb/render/scene/SceneLocalReflectionAtlas.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "kb/render/ViewIdPolicy.hpp"
#include "renderer/RendererDebugLog.hpp"

#include <algorithm>
#include <cstring>

namespace kb::render {
namespace {

using Layout = LocalReflectionAtlasLayout;

struct TileVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
};

[[nodiscard]] bgfx::VertexLayout TileVertexLayout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    return layout;
}

template <typename Handle>
void DestroyHandle(Handle& handle) noexcept {
    if (bgfx::isValid(handle)) {
        bgfx::destroy(handle);
        handle = BGFX_INVALID_HANDLE;
    }
}

[[nodiscard]] bool HalfFloatTargetFilterable() noexcept {
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr) return false;
    const std::uint32_t support = caps->formats[bgfx::TextureFormat::RGBA16F];
    return (support & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0U && (support & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0U;
}

} // namespace

SceneLocalReflectionAtlas::SceneLocalReflectionAtlas() = default;

SceneLocalReflectionAtlas::~SceneLocalReflectionAtlas() {
    Shutdown();
}

bool SceneLocalReflectionAtlas::Initialize() {
    if (IsInitialized()) return true;
    if (!HalfFloatTargetFilterable()) {
        WriteRendererDebugLog("local_reflection",
            "Local reflection probes unavailable: this backend cannot render to a filterable half-float texture");
        return false;
    }
    program_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_local_reflection_atlas.sc");
    if (!bgfx::isValid(program_)) {
        WriteRendererDebugLog("local_reflection",
            "Local reflection probes unavailable: fs_local_reflection_atlas.sc has no variant for this backend");
        Shutdown();
        return false;
    }
    sourceSampler_ = bgfx::createUniform("s_localReflectionSource", bgfx::UniformType::Sampler);
    buildUniform_ = bgfx::createUniform("u_localReflectionBuild", bgfx::UniformType::Vec4);
    atlas_ = bgfx::createTexture2D(static_cast<std::uint16_t>(Layout::Width()), static_cast<std::uint16_t>(Layout::Height()), false, 1,
        bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    if (bgfx::isValid(atlas_)) {
        bgfx::setName(atlas_, "KB Local Reflection Atlas");
        frameBuffer_ = bgfx::createFrameBuffer(1, &atlas_, false);
    }
    if (!IsInitialized()) {
        WriteRendererDebugLog("local_reflection", "Local reflection probes unavailable: the atlas could not be created");
        Shutdown();
        return false;
    }
    return true;
}

void SceneLocalReflectionAtlas::Shutdown() noexcept {
    DestroyHandle(frameBuffer_);
    DestroyHandle(atlas_);
    DestroyHandle(buildUniform_);
    DestroyHandle(sourceSampler_);
    DestroyHandle(program_);
    keys_.fill(Key{});
    shown_.fill(false);
    pending_.fill(false);
}

bool SceneLocalReflectionAtlas::IsInitialized() const noexcept {
    return bgfx::isValid(program_) && bgfx::isValid(sourceSampler_) && bgfx::isValid(buildUniform_) && bgfx::isValid(atlas_) &&
        bgfx::isValid(frameBuffer_);
}

SceneLocalReflectionAtlas::Key SceneLocalReflectionAtlas::KeyOf(const SceneRenderReflectionProbe& probe) noexcept {
    return Key{.texture = probe.radianceTexture.idx, .maxMip = probe.maxMip};
}

int SceneLocalReflectionAtlas::FindSlot(const Key& key) const noexcept {
    for (std::uint32_t slot = 0U; slot < Layout::kSlotCount; ++slot) {
        if (keys_[slot] == key) return static_cast<int>(slot);
    }
    return -1;
}

bool SceneLocalReflectionAtlas::SubmitSlot(std::uint32_t slot, bgfx::TextureHandle source, std::uint8_t maxMip) const {
    const bgfx::VertexLayout layout = TileVertexLayout();
    const bool originBottomLeft = bgfx::getCaps()->originBottomLeft;
    const float width = static_cast<float>(Layout::Width());
    const float height = static_cast<float>(Layout::Height());
    const bgfx::ViewId view = static_cast<bgfx::ViewId>(ViewId::LocalReflectionAtlas);
    constexpr std::uint32_t kVerticesPerSlot = 6U * Layout::kLevelCount;
    if (bgfx::getAvailTransientVertexBuffer(kVerticesPerSlot, layout) < kVerticesPerSlot) return false;
    bgfx::setViewName(view, "KB Local Reflection Atlas");
    bgfx::setViewFrameBuffer(view, frameBuffer_);
    bgfx::setViewRect(view, 0, 0, static_cast<std::uint16_t>(Layout::Width()), static_cast<std::uint16_t>(Layout::Height()));
    bgfx::setViewClear(view, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(view, nullptr, nullptr);
    for (std::uint32_t level = 0U; level < Layout::kLevelCount; ++level) {
        const float x0 = static_cast<float>(Layout::SlotX(slot) + Layout::LevelX(level));
        const float y0 = static_cast<float>(Layout::SlotY(slot) + Layout::LevelY(level));
        const float tile = static_cast<float>(Layout::TileSize(level));
        const float left = -1.0F + 2.0F * x0 / width;
        const float right = -1.0F + 2.0F * (x0 + tile) / width;
        // Memory rows run top-down on Direct3D-style targets and bottom-up on OpenGL ones; the tile's v follows the
        // memory rows (vs_present.sc turns v around on OpenGL).
        const float rowTop = originBottomLeft ? -1.0F + 2.0F * (y0 + tile) / height : 1.0F - 2.0F * y0 / height;
        const float rowBottom = originBottomLeft ? -1.0F + 2.0F * y0 / height : 1.0F - 2.0F * (y0 + tile) / height;
        const std::array<TileVertex, 6> quad{
            TileVertex{left, rowTop, 0.0F, 0.0F, 0.0F},    TileVertex{right, rowTop, 0.0F, 1.0F, 0.0F},
            TileVertex{right, rowBottom, 0.0F, 1.0F, 1.0F}, TileVertex{left, rowTop, 0.0F, 0.0F, 0.0F},
            TileVertex{right, rowBottom, 0.0F, 1.0F, 1.0F}, TileVertex{left, rowBottom, 0.0F, 0.0F, 1.0F},
        };
        bgfx::TransientVertexBuffer vertices{};
        bgfx::allocTransientVertexBuffer(&vertices, 6, layout);
        std::memcpy(vertices.data, quad.data(), sizeof(TileVertex) * quad.size());
        // The level that reflects this roughness reads the source cube's mip chain at the same fraction of it.
        const float roughness = static_cast<float>(level) / static_cast<float>(Layout::kLevelCount - 1U);
        const std::array<float, 4> build{roughness * static_cast<float>(maxMip > 0U ? maxMip - 1U : 0U),
            static_cast<float>(Layout::LevelSize(level)), static_cast<float>(Layout::kGutter), 0.0F};
        bgfx::setUniform(buildUniform_, build.data());
        bgfx::setTexture(0U, sourceSampler_, source);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::setVertexBuffer(0, &vertices);
        bgfx::submit(view, program_);
    }
    return true;
}

void SceneLocalReflectionAtlas::Update(const SceneRenderIblConfig& ibl) {
    const std::uint32_t count = std::min(ibl.reflectionProbeCount, Layout::kSlotCount);
    std::array<Key, Layout::kSlotCount> wanted{};
    std::uint32_t wantedCount = 0U;
    for (std::uint32_t index = 0U; index < count; ++index) {
        const SceneRenderReflectionProbe& probe = ibl.reflectionProbes[index];
        if (!probe.IsValid()) continue;
        const Key key = KeyOf(probe);
        if (std::find(wanted.begin(), wanted.begin() + wantedCount, key) == wanted.begin() + wantedCount) wanted[wantedCount++] = key;
    }
    if (wantedCount == 0U || !Initialize()) return;
    const auto isWanted = [&](const Key& key) { return std::find(wanted.begin(), wanted.begin() + wantedCount, key) != wanted.begin() + wantedCount; };
    for (std::uint32_t entry = 0U; entry < wantedCount; ++entry) {
        const Key key = wanted[entry];
        if (FindSlot(key) >= 0) continue;
        // A slot nobody asks for now: never one a probe of this frame still reads.
        std::uint32_t slot = Layout::kSlotCount;
        for (std::uint32_t candidate = 0U; candidate < Layout::kSlotCount; ++candidate) {
            if (!keys_[candidate].Valid()) { slot = candidate; break; }
            if (slot == Layout::kSlotCount && !isWanted(keys_[candidate]) && !pending_[candidate]) slot = candidate;
        }
        if (slot == Layout::kSlotCount) continue;
        if (!SubmitSlot(slot, bgfx::TextureHandle{key.texture}, key.maxMip)) {
            WriteRendererDebugLog("local_reflection", "Local reflection copy postponed: no transient vertices left this frame");
            continue;
        }
        keys_[slot] = key;
        shown_[slot] = false;
        pending_[slot] = true;
    }
}

void SceneLocalReflectionAtlas::Apply(SceneRenderIblConfig& ibl) const noexcept {
    const std::uint32_t count = std::min(ibl.reflectionProbeCount, Layout::kSlotCount);
    const bool ready = IsInitialized();
    ibl.reflectionAtlas = ready ? atlas_ : bgfx::TextureHandle{bgfx::kInvalidHandle};
    for (std::uint32_t index = 0U; index < count; ++index) {
        SceneRenderReflectionProbe& probe = ibl.reflectionProbes[index];
        probe.atlasReady = false;
        probe.atlasSlot = 0U;
        if (!ready || !probe.IsValid()) continue;
        const int slot = FindSlot(KeyOf(probe));
        if (slot < 0) continue;
        probe.atlasSlot = static_cast<std::uint8_t>(slot);
        probe.atlasReady = shown_[static_cast<std::size_t>(slot)];
    }
}

void SceneLocalReflectionAtlas::EndFrame() noexcept {
    for (std::uint32_t slot = 0U; slot < Layout::kSlotCount; ++slot) {
        if (!pending_[slot]) continue;
        pending_[slot] = false;
        shown_[slot] = true;
    }
}

} // namespace kb::render
