#include "RendererTestSupport.hpp"
#include "engine/particles/ParticleGpuEmitter.hpp"
#include "engine/particles/ParticleRenderSnapshot.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/particles/ParticleGpuEmitterSimulation.hpp"
#include "kb/render/particles/ParticleStripGeometryBuilder.hpp"
#include "kb/render/particles/ParticleStripRenderer.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <span>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace kb::render::tests {
namespace {
#if defined(_WIN32)
using kb::particles::ParticleRenderOutput;

constexpr float kStep = 1.0F / 60.0F;
constexpr std::uint32_t kFloatsPerVertex = 12U;
constexpr std::uint32_t kImageSize = 64U;

class StripSurface final : public RenderSurface {
public:
    StripSurface() : window_(CreateWindowExW(0U, L"STATIC", L"Particle strip readback", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)) {}
    ~StripSurface() override { if (window_ != nullptr) DestroyWindow(window_); }
    StripSurface(const StripSurface&) = delete;
    StripSurface& operator=(const StripSurface&) = delete;
    std::uint32_t Width() const noexcept override { return 64U; }
    std::uint32_t Height() const noexcept override { return 64U; }
    void* NativeWindowHandle() const noexcept override { return window_; }
    void* NativeDisplayHandle() const noexcept override { return nullptr; }
private:
    HWND window_ = nullptr;
};

[[nodiscard]] SceneRenderCamera MakeCamera() {
    SceneRenderCamera camera{};
    const bx::Vec3 eye{0.5F, 1.5F, -7.0F};
    const bx::Vec3 at{1.5F, 1.0F, 0.0F};
    bx::mtxLookAt(camera.view.data(), eye, at);
    bx::mtxProj(camera.projection.data(), 60.0F, 1.0F, 0.1F, 100.0F, bgfx::getCaps()->homogeneousDepth);
    return camera;
}

[[nodiscard]] ParticleGpuEmitterSimulation::FrameContext MakeFrame(const SceneRenderCamera& camera) {
    ParticleGpuEmitterSimulation::FrameContext frame;
    frame.cameraDepth = {camera.view[2], camera.view[6], camera.view[10], camera.view[14]};
    frame.cameraRight = {camera.view[0], camera.view[4], camera.view[8]};
    return frame;
}

// Reads the strip vertices of a GPU draw: kFloatsPerVertex floats each (position, colour, texture coordinate).
[[nodiscard]] std::vector<float> ReadStrip(const ParticleGpuEmitterSimulation::Draw& draw) {
    Require(draw.stripSegments != 0U && bgfx::isValid(draw.stripVertices) && bgfx::isValid(draw.stripIndices),
        "A ribbon or beam draw carries no strip");
    std::vector<float> floats(static_cast<std::size_t>(draw.stripSegments) * 4U * kFloatsPerVertex,
        std::numeric_limits<float>::quiet_NaN());
    const bgfx::BufferRegion region{.handle = draw.stripVertices,
        .size = static_cast<std::uint32_t>(floats.size() * sizeof(float))};
    const auto ready = bgfx::read(region, floats.data());
    auto frame = bgfx::frame();
    while (frame < ready) frame = bgfx::frame();
    for (const float value : floats) Require(std::isfinite(value), "The GPU strip readback returned unwritten data");
    return floats;
}

[[nodiscard]] float VertexComponent(const std::vector<float>& strip, std::size_t vertex, std::size_t lane) {
    return strip[vertex * kFloatsPerVertex + lane];
}

[[nodiscard]] float PositionDistance(const std::vector<float>& strip, std::size_t gpuVertex, const ParticleStripVertex& cpu) {
    return std::max({std::abs(VertexComponent(strip, gpuVertex, 0U) - cpu.x), std::abs(VertexComponent(strip, gpuVertex, 1U) - cpu.y),
        std::abs(VertexComponent(strip, gpuVertex, 2U) - cpu.z)});
}

[[nodiscard]] std::shared_ptr<const kb::particles::ParticleRenderSnapshot> Publish(
    kb::particles::ParticleRenderSnapshotChannel& channel, std::uint64_t step,
    std::span<const kb::particles::ParticleRenderEmitterRecord> emitters,
    std::span<const kb::particles::ParticleRenderRecord> particles) {
    const auto published = channel.Publish(1U, {.revision = step, .fixedStepIndex = step, .emitters = emitters, .particles = particles});
    if (!published.Succeeded()) std::fprintf(stderr, "snapshot status %d at step %llu\n", static_cast<int>(published.status),
        static_cast<unsigned long long>(step));
    Require(published.Succeeded(), "The strip comparison snapshot was not published");
    return channel.Read();
}

[[nodiscard]] kb::particles::ParticleRenderEmitterRecord RibbonRecord(std::uint32_t count, float width) {
    kb::particles::ParticleRenderEmitterRecord record{};
    record.instanceId = 1U;
    record.effectAssetId = 2U;
    record.assetGeneration = 1U;
    record.materialAssetId = 5U;
    record.emitterId = 1U;
    record.particleCount = count;
    record.liveParticleCount = count;
    record.output = ParticleRenderOutput::Ribbon;
    record.blend = kb::particles::ParticleRenderBlendMode::Alpha;
    record.status = kb::particles::ParticleRenderEmitterStatus::Playing;
    record.ribbonMaxSegments = 8U;
    record.ribbonWidth = width;
    record.ribbonBreakOnDeath = true;
    return record;
}

[[nodiscard]] kb::particles::ParticleRenderEmitterRecord BeamRecord() {
    kb::particles::ParticleRenderEmitterRecord record{};
    record.instanceId = 1U;
    record.effectAssetId = 2U;
    record.assetGeneration = 1U;
    record.materialAssetId = 5U;
    record.emitterId = 2U;
    record.output = ParticleRenderOutput::Beam;
    record.blend = kb::particles::ParticleRenderBlendMode::Alpha;
    record.status = kb::particles::ParticleRenderEmitterStatus::Playing;
    record.beamSegments = 12U;
    record.beamWidth = 0.3F;
    record.beamNoiseAmplitude = 0.4F;
    record.beamNoiseFrequency = 2.0F;
    record.colorIntensity = 2.0F;
    record.outputOrigin = {0.0F, 0.0F, 0.0F};
    record.beamEnd = {3.0F, 2.0F, 0.5F};
    record.beamLocalEnd = {3.0F, 2.0F, 0.5F};
    return record;
}

struct RibbonScene {
    static constexpr std::uint32_t kLive = 5U;
    static constexpr float kLifetime = 2.0F;
    static constexpr float kNow = 0.5F;
    std::array<kb::particles::ParticleRenderRecord, kLive> particles{};
    kb::particles::ParticleGpuEmitterCommand command{};

    RibbonScene() {
        command.key = {1U, 1U};
        command.simTime = kNow;
        command.hasParams = true;
        command.params.output = ParticleRenderOutput::Ribbon;
        command.params.capacity = 8U;
        command.params.ribbonWidth = 0.25F;
        command.params.size.fill(0.0F); // a ribbon takes its width from its output, not from the size curve
        command.params.color.fill({0.5F, 0.25F, 1.0F, 0.8F});
        for (std::uint32_t index = 0U; index < kLive; ++index) {
            const float fi = static_cast<float>(index);
            const float birth = 0.1F * fi;
            const kb::math::Vec3 start{fi, 0.1F * fi * fi, 0.5F * fi};
            const kb::math::Vec3 velocity{0.0F, 1.0F, 0.2F};
            command.spawns.push_back({.position = start, .birthTime = birth, .velocity = velocity, .lifetime = kLifetime});
            const float age = kNow - birth;
            particles[index].position = {start.x + velocity.x * age, start.y + velocity.y * age, start.z + velocity.z * age};
            particles[index].particleId = index + 1U;
            particles[index].spawnOrdinal = index;
        }
    }
};

void Dispatch(ParticleGpuEmitterSimulation& simulation, const SceneRenderCamera& camera,
    const kb::particles::ParticleGpuEmitterCommand& command) {
    simulation.Apply(1U, {&command, 1U});
    simulation.Advance(1U, kStep);
    simulation.Dispatch(0U, 1U, MakeFrame(camera));
    static_cast<void>(bgfx::frame());
}

void VerifyGpuRibbonMatchesCpu(const SceneRenderCamera& camera) {
    RibbonScene scene;
    ParticleGpuEmitterSimulation simulation;
    Require(simulation.Initialize(), "The GPU emitter kernels, including the strip kernel, are unavailable");
    Dispatch(simulation, camera, scene.command);
    Require(simulation.Draws(1U).size() == 1U && simulation.Draws(1U)[0].stripSegments == 8U,
        "The ribbon emitter did not produce a strip of one segment per ring slot");
    const std::vector<float> gpu = ReadStrip(simulation.Draws(1U)[0]);

    kb::particles::ParticleRenderSnapshotChannel channel;
    Require(channel.Warmup(7U).Succeeded(), "The strip comparison channel did not warm up");
    auto emitter = RibbonRecord(RibbonScene::kLive, 0.25F);
    for (auto& particle : scene.particles) particle.packedColor = 0xFFFFFFFFU;
    const auto snapshot = Publish(channel, 1U, std::span{&emitter, 1U}, scene.particles);
    ParticleStripGeometryBuilder builder;
    builder.Warmup();
    const ParticleStripBuildResult cpu = builder.Build(*snapshot, camera);
    Require(cpu.Succeeded() && cpu.vertices.size() == 2U * RibbonScene::kLive, "The CPU ribbon reference was not built");

    // Segment s of the GPU strip joins the live slots s and s + 1: the four vertices of the CPU strip at the same joint.
    float worst = 0.0F;
    float previousU = -1.0F;
    for (std::size_t segment = 0U; segment + 1U < RibbonScene::kLive; ++segment) {
        for (std::size_t corner = 0U; corner < 4U; ++corner) {
            worst = std::max(worst, PositionDistance(gpu, segment * 4U + corner, cpu.vertices[segment * 2U + corner]));
            Require(NearlyEqual(VertexComponent(gpu, segment * 4U + corner, 4U), 0.5F) &&
                    NearlyEqual(VertexComponent(gpu, segment * 4U + corner, 5U), 0.25F) &&
                    NearlyEqual(VertexComponent(gpu, segment * 4U + corner, 6U), 1.0F) &&
                    NearlyEqual(VertexComponent(gpu, segment * 4U + corner, 7U), 0.8F),
                "The GPU ribbon lost the colour of its emitter");
        }
        const float u = VertexComponent(gpu, segment * 4U, 8U);
        Require(u > previousU && u >= 0.0F && u <= 1.0F && VertexComponent(gpu, segment * 4U, 9U) == 0.0F &&
                VertexComponent(gpu, segment * 4U + 1U, 9U) == 1.0F,
            "The GPU ribbon's texture coordinate does not run along the strip");
        previousU = u;
    }
    Require(worst < 0.0005F, "The GPU ribbon is not where the CPU ribbon puts it");
    // The slots that hold no live particle draw nothing: their quads collapse to a point at the origin.
    for (std::size_t vertex = (RibbonScene::kLive - 1U) * 4U; vertex < gpu.size() / kFloatsPerVertex; ++vertex) {
        for (const std::size_t lane : {0U, 1U, 2U, 4U, 5U, 6U, 7U}) {
            Require(VertexComponent(gpu, vertex, lane) == 0.0F, "A dead ribbon slot left geometry behind");
        }
    }
    simulation.Shutdown();
    std::fprintf(stderr, "particle_gpu_ribbon segments=%zu max_position_error=%.6f dead_slots=collapsed\n",
        static_cast<std::size_t>(RibbonScene::kLive - 1U), static_cast<double>(worst));
}

void VerifyGpuBeamMatchesCpu(const SceneRenderCamera& camera) {
    constexpr std::uint64_t kClockStep = 37U;
    ParticleGpuEmitterSimulation simulation;
    Require(simulation.Initialize(), "The GPU emitter kernels, including the strip kernel, are unavailable");
    const auto record = BeamRecord();
    kb::particles::ParticleGpuEmitterCommand command;
    command.key = {1U, 2U};
    command.simTime = static_cast<double>(kClockStep) / 60.0;
    command.hasParams = true;
    command.params.output = ParticleRenderOutput::Beam;
    command.params.capacity = 1U;
    command.params.beamSegments = record.beamSegments;
    command.params.beamWidth = record.beamWidth;
    command.params.beamNoiseAmplitude = record.beamNoiseAmplitude;
    command.params.beamNoiseFrequency = record.beamNoiseFrequency;
    command.params.beamIntensity = record.colorIntensity;
    command.hasBeam = true;
    command.beamStart = record.outputOrigin;
    command.beamEnd = record.beamEnd;
    Dispatch(simulation, camera, command);
    Require(simulation.Draws(1U).size() == 1U && simulation.Draws(1U)[0].stripSegments == record.beamSegments,
        "The beam emitter did not produce a strip of its segments");
    const std::vector<float> gpu = ReadStrip(simulation.Draws(1U)[0]);

    kb::particles::ParticleRenderSnapshotChannel channel;
    Require(channel.Warmup(8U).Succeeded(), "The beam comparison channel did not warm up");
    const auto snapshot = Publish(channel, kClockStep, std::span{&record, 1U}, {});
    ParticleStripGeometryBuilder builder;
    builder.Warmup();
    const ParticleStripBuildResult cpu = builder.Build(*snapshot, camera);
    Require(cpu.Succeeded() && cpu.vertices.size() == 2U * (record.beamSegments + 1U), "The CPU beam reference was not built");

    float worst = 0.0F;
    float middleOffset = 0.0F;
    for (std::size_t segment = 0U; segment < record.beamSegments; ++segment) {
        for (std::size_t corner = 0U; corner < 4U; ++corner) {
            const std::size_t gpuVertex = segment * 4U + corner;
            worst = std::max(worst, PositionDistance(gpu, gpuVertex, cpu.vertices[segment * 2U + corner]));
            Require(NearlyEqual(VertexComponent(gpu, gpuVertex, 4U), 2.0F) && NearlyEqual(VertexComponent(gpu, gpuVertex, 7U), 1.0F) &&
                    std::abs(VertexComponent(gpu, gpuVertex, 8U) - cpu.vertices[segment * 2U + corner].u) < 0.0001F &&
                    VertexComponent(gpu, gpuVertex, 9U) == static_cast<float>(corner & 1U),
                "The GPU beam's colour intensity or texture coordinates differ from the CPU beam's");
        }
    }
    // The noise really moves the beam: its middle is off the straight line between the ends.
    const std::size_t middle = (record.beamSegments / 2U) * 4U;
    const kb::math::Vec3 centre{(VertexComponent(gpu, middle, 0U) + VertexComponent(gpu, middle + 1U, 0U)) * 0.5F,
        (VertexComponent(gpu, middle, 1U) + VertexComponent(gpu, middle + 1U, 1U)) * 0.5F,
        (VertexComponent(gpu, middle, 2U) + VertexComponent(gpu, middle + 1U, 2U)) * 0.5F};
    const kb::math::Vec3 line = record.beamEnd - record.outputOrigin;
    const float along = kb::math::Dot(centre, line) / kb::math::Dot(line, line);
    const kb::math::Vec3 off = centre - line * along;
    middleOffset = std::sqrt(kb::math::Dot(off, off));
    Require(worst < 0.001F, "The GPU beam is not where the CPU beam puts it");
    Require(middleOffset > 0.05F, "The GPU beam did not bend with its noise");
    simulation.Shutdown();
    std::fprintf(stderr, "particle_gpu_beam segments=%u max_position_error=%.6f noise_offset=%.4f\n",
        record.beamSegments, static_cast<double>(worst), static_cast<double>(middleOffset));
}

void VerifyCpuBuildIsSkippedForGpuEmitters(const SceneRenderCamera& camera) {
    kb::particles::ParticleRenderSnapshotChannel channel;
    Require(channel.Warmup(9U).Succeeded(), "The skip channel did not warm up");
    std::array<kb::particles::ParticleRenderRecord, 3U> particles{};
    for (std::uint32_t index = 0U; index < 3U; ++index) {
        particles[index].position = {static_cast<float>(index), 0.0F, 0.0F};
        particles[index].particleId = index + 1U;
        particles[index].spawnOrdinal = index;
    }
    auto ribbon = RibbonRecord(0U, 0.25F);
    auto beam = BeamRecord();
    ribbon.simulationPath = kb::particles::ParticleEmitterSimulationPath::Gpu;
    ribbon.gpuFallbackReason = kb::particles::ParticleGpuFallbackReason::None;
    beam.simulationPath = kb::particles::ParticleEmitterSimulationPath::Gpu;
    beam.gpuFallbackReason = kb::particles::ParticleGpuFallbackReason::None;
    std::array gpuEmitters{ribbon, beam};
    ParticleStripGeometryBuilder builder;
    builder.Warmup();
    const ParticleStripBuildResult skipped = builder.Build(*Publish(channel, 1U, gpuEmitters, {}), camera);
    Require(skipped.Succeeded() && skipped.vertices.empty() && skipped.draws.empty() && builder.CpuRibbonBeamBuilds() == 0U,
        "The CPU built the ribbon or beam of a GPU-simulated emitter");
    // The same emitters on the CPU path are built, and counted.
    gpuEmitters[0].simulationPath = kb::particles::ParticleEmitterSimulationPath::Cpu;
    gpuEmitters[0].gpuFallbackReason = kb::particles::ParticleGpuFallbackReason::Output;
    gpuEmitters[0].particleCount = 3U;
    gpuEmitters[0].liveParticleCount = 3U;
    gpuEmitters[1].firstParticle = 3U;
    gpuEmitters[1].simulationPath = kb::particles::ParticleEmitterSimulationPath::Cpu;
    gpuEmitters[1].gpuFallbackReason = kb::particles::ParticleGpuFallbackReason::Output;
    const ParticleStripBuildResult built = builder.Build(*Publish(channel, 2U, gpuEmitters, particles), camera);
    Require(built.Succeeded() && !built.vertices.empty() && built.draws.size() == 2U && builder.CpuRibbonBeamBuilds() == 2U,
        "The CPU did not build the ribbon and beam of CPU-simulated emitters");
    // Particles a GPU emitter still holds on the CPU after a live edit finish there.
    for (auto& emitter : gpuEmitters) {
        emitter.simulationPath = kb::particles::ParticleEmitterSimulationPath::Gpu;
        emitter.gpuFallbackReason = kb::particles::ParticleGpuFallbackReason::None;
    }
    const ParticleStripBuildResult retained = builder.Build(*Publish(channel, 3U, gpuEmitters, particles), camera);
    Require(retained.Succeeded() && retained.draws.size() == 1U && builder.CpuRibbonBeamBuilds() == 3U,
        "The retained CPU particles of a GPU emitter lost their ribbon, or its beam was built");
    std::fprintf(stderr, "particle_gpu_strip_cpu_builds gpu_emitters=0 cpu_emitters=2 retained_cpu_particles=1\n");
}

// Draws strips into a small target and returns its RGBA bytes.
class StripImage final {
public:
    StripImage() {
        color_ = bgfx::createTexture2D(kImageSize, kImageSize, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
        frameBuffer_ = bgfx::createFrameBuffer(1U, &color_, false);
        readback_ = bgfx::createTexture2D(kImageSize, kImageSize, false, 1U, bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        const std::uint32_t white = 0xFFFFFFFFU;
        white_ = bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, bgfx::copy(&white, sizeof(white)));
        Require(bgfx::isValid(frameBuffer_) && bgfx::isValid(readback_) && bgfx::isValid(white_) && strips_.Initialize(),
            "The strip image targets or the strip renderer are unavailable");
    }
    ~StripImage() {
        strips_.Shutdown();
        bgfx::destroy(white_); bgfx::destroy(readback_); bgfx::destroy(frameBuffer_);
    }
    StripImage(const StripImage&) = delete;
    StripImage& operator=(const StripImage&) = delete;
    [[nodiscard]] ParticleStripRenderer& Strips() noexcept { return strips_; }
    [[nodiscard]] bgfx::TextureHandle White() const noexcept { return white_; }

    // `draw` submits into the view it is given.
    template <typename Draw>
    [[nodiscard]] std::vector<std::uint8_t> Render(const SceneRenderCamera& camera, Draw&& draw) {
        constexpr bgfx::ViewId view = 2U;
        bgfx::setViewFrameBuffer(view, frameBuffer_);
        bgfx::setViewRect(view, 0U, 0U, kImageSize, kImageSize);
        bgfx::setViewClear(view, BGFX_CLEAR_COLOR, 0x000000FFU, 1.0F, 0U);
        bgfx::setViewTransform(view, camera.view.data(), camera.projection.data());
        bgfx::touch(view);
        draw(view);
        bgfx::blit(view + 1U, bgfx::TextureRegion{.handle = readback_},
            bgfx::TextureRegion{.handle = color_, .width = kImageSize, .height = kImageSize});
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kImageSize) * kImageSize * 4U, 0U);
        const auto ready = bgfx::read(bgfx::TextureRegion{.handle = readback_}, pixels.data());
        auto frame = bgfx::frame();
        while (frame < ready) frame = bgfx::frame();
        return pixels;
    }

private:
    ParticleStripRenderer strips_;
    bgfx::TextureHandle color_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle readback_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
};

struct Coverage {
    std::size_t covered = 0U;
    std::size_t shared = 0U;
    std::size_t either = 0U;
};

[[nodiscard]] Coverage Compare(const std::vector<std::uint8_t>& lhs, const std::vector<std::uint8_t>& rhs) {
    Coverage result;
    for (std::size_t pixel = 0U; pixel < lhs.size() / 4U; ++pixel) {
        const auto lit = [&](const std::vector<std::uint8_t>& image) {
            return image[pixel * 4U] > 8U || image[pixel * 4U + 1U] > 8U || image[pixel * 4U + 2U] > 8U;
        };
        const bool a = lit(lhs);
        const bool b = lit(rhs);
        result.covered += a ? 1U : 0U;
        result.shared += a && b ? 1U : 0U;
        result.either += a || b ? 1U : 0U;
    }
    return result;
}

void VerifyGpuStripsDrawLikeCpuStrips(const SceneRenderCamera& camera) {
    RibbonScene scene;
    ParticleGpuEmitterSimulation simulation;
    Require(simulation.Initialize(), "The GPU emitter kernels, including the strip kernel, are unavailable");
    scene.command.params.ribbonWidth = 0.4F;
    scene.command.params.color.fill({1.0F, 1.0F, 1.0F, 1.0F});
    const auto beamRecord = BeamRecord();
    kb::particles::ParticleGpuEmitterCommand beam;
    beam.key = {1U, 2U};
    beam.simTime = scene.command.simTime;
    beam.hasParams = true;
    beam.params.output = ParticleRenderOutput::Beam;
    beam.params.capacity = 1U;
    beam.params.beamSegments = beamRecord.beamSegments;
    beam.params.beamWidth = 0.4F;
    beam.params.beamIntensity = 1.0F;
    beam.hasBeam = true;
    beam.beamStart = {0.0F, 0.0F, 0.0F};
    beam.beamEnd = {3.0F, 2.0F, 0.5F};
    const std::array commands{scene.command, beam};
    simulation.Apply(1U, commands);
    simulation.Advance(1U, kStep);
    simulation.Dispatch(0U, 1U, MakeFrame(camera));
    static_cast<void>(bgfx::frame());
    Require(simulation.Draws(1U).size() == 2U, "The ribbon and the beam were not both dispatched");

    StripImage image;
    const auto gpuImage = image.Render(camera, [&](bgfx::ViewId view) {
        for (const auto& draw : simulation.Draws(1U)) {
            Require(image.Strips().SubmitGpuStrip(view, draw.stripVertices, draw.stripIndices, draw.stripSegments,
                        draw.params->blend, kb::particles::ParticleRenderDepthMode::Disabled, image.White()).succeeded,
                "The GPU strip was not submitted");
        }
    });

    // The CPU path of the same strips.
    auto ribbon = RibbonRecord(RibbonScene::kLive, 0.4F);
    auto beamCpu = BeamRecord();
    beamCpu.beamWidth = 0.4F;
    beamCpu.beamNoiseAmplitude = 0.0F;
    beamCpu.colorIntensity = 1.0F;
    for (auto& particle : scene.particles) particle.packedColor = 0xFFFFFFFFU;
    kb::particles::ParticleRenderSnapshotChannel channel;
    Require(channel.Warmup(10U).Succeeded(), "The image comparison channel did not warm up");
    std::array emitters{ribbon, beamCpu};
    emitters[1].firstParticle = RibbonScene::kLive;
    emitters[1].depth = kb::particles::ParticleRenderDepthMode::Disabled;
    emitters[0].depth = kb::particles::ParticleRenderDepthMode::Disabled;
    const auto snapshot = Publish(channel, 30U, emitters, scene.particles);
    const auto cpuImage = image.Render(camera, [&](bgfx::ViewId view) {
        const ParticleStripBuildResult& build = image.Strips().Build(*snapshot, camera);
        Require(build.Succeeded() && build.draws.size() == 2U, "The CPU strips were not built for the image comparison");
        for (std::uint32_t draw = 0U; draw < build.draws.size(); ++draw) {
            Require(image.Strips().SubmitDraw(view, draw, image.White()).succeeded, "A CPU strip was not submitted");
        }
    });
    const Coverage coverage = Compare(gpuImage, cpuImage);
    std::fprintf(stderr, "particle_gpu_strip_image gpu_pixels=%zu shared=%zu either=%zu\n", coverage.covered, coverage.shared, coverage.either);
    Require(coverage.covered > 100U, "The GPU ribbon and beam covered too few pixels");
    // The beam of the GPU path bends (its noise amplitude is the CPU reference's zero here only for the straight comparison):
    // the images are compared by the pixels both light, which must be nearly all of them.
    Require(coverage.shared * 100U >= coverage.either * 80U, "The GPU strips cover other pixels than the CPU strips");
    simulation.Shutdown();
}
#endif
} // namespace

void RunParticleGpuStripTests(bgfx::RendererType::Enum backend) {
#if defined(_WIN32)
    StripSurface surface;
    DisplayConfig config;
    config.syncMode = DisplaySyncMode::Uncapped;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(backend);
    Renderer renderer;
    Require(surface.NativeWindowHandle() != nullptr && renderer.Initialize(surface, &config), "Requested real bgfx device unavailable");
    Require(bgfx::getRendererType() == backend, "The strip test used an unexpected backend");
    {
        const SceneRenderCamera camera = MakeCamera();
        VerifyCpuBuildIsSkippedForGpuEmitters(camera);
        VerifyGpuRibbonMatchesCpu(camera);
        VerifyGpuBeamMatchesCpu(camera);
        VerifyGpuStripsDrawLikeCpuStrips(camera);
    }
    renderer.Shutdown();
#else
    static_cast<void>(backend);
    Require(false, "The GPU strip verification requires the Windows GPU host");
#endif
}

} // namespace kb::render::tests
