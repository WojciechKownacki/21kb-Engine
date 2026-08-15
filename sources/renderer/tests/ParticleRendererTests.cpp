#include "engine/particles/ParticleRenderSnapshot.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/particles/ParticleMeshBatchBuilder.hpp"
#include "kb/render/particles/ParticleRenderBatcher.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/TransparentDepthKey.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::atomic<bool> g_measureAllocations{false};
std::atomic<std::size_t> g_allocations{0U};

void Require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error{std::string{message}};
}

[[nodiscard]] kb::particles::ParticleRenderRecord Particle(
    std::uint64_t id, float x, float z, std::uint16_t age) noexcept {
    return {
        .position = {x, 0.0F, z},
        .size = 1.0F,
        .previousPosition = {x - 0.25F, 0.0F, z},
        .velocity = {1.0F, 0.0F, 0.0F},
        .particleId = id,
        .packedColor = 0x80402010U,
        .frame = 7U,
        .normalizedAgeUnorm = age,
    };
}

[[nodiscard]] kb::particles::ParticleRenderEmitterRecord Emitter(
    std::uint32_t first, std::uint32_t count,
    kb::particles::ParticleRenderSortMode sort,
    kb::particles::ParticleRenderBlendMode blend = kb::particles::ParticleRenderBlendMode::Alpha) noexcept {
    return {
        .instanceId = 1U,
        .effectAssetId = 2U,
        .emitterId = 3U,
        .assetGeneration = 4U,
        .materialAssetId = 5U,
        .textureAtlasAssetId = 6U,
        .firstParticle = first,
        .particleCount = count,
        .liveParticleCount = count,
        .output = kb::particles::ParticleRenderOutput::Billboard,
        .blend = blend,
        .depth = kb::particles::ParticleRenderDepthMode::ReadOnly,
        .sort = sort,
        .status = kb::particles::ParticleRenderEmitterStatus::Playing,
        .alignment = kb::particles::ParticleRenderAlignment::CameraFacing,
        .flags = kb::particles::ParticleRenderEmitterFlag::SoftParticles,
        .flipbookColumnsEncoded = 4U,
        .flipbookRowsEncoded = 2U,
        .pointSpriteDiameter = 1.0F,
        .boundsMinimum = {-10.0F, -10.0F, -10.0F},
        .boundsMaximum = {10.0F, 10.0F, 10.0F},
    };
}

[[nodiscard]] std::shared_ptr<const kb::particles::ParticleRenderSnapshot> Snapshot(
    std::span<const kb::particles::ParticleRenderEmitterRecord> emitters,
    std::span<const kb::particles::ParticleRenderRecord> particles) {
    kb::particles::ParticleRenderSnapshotChannel channel;
    Require(channel.Warmup(91U).Succeeded(), "snapshot warmup failed");
    Require(channel.Publish(8U, {.revision = 1U, .fixedStepIndex = 12U,
        .emitters = emitters, .particles = particles}).Succeeded(), "snapshot publication failed");
    return channel.Read();
}

[[nodiscard]] kb::render::SceneRenderCamera IdentityCamera() noexcept {
    kb::render::SceneRenderCamera camera{};
    camera.view[0] = camera.view[5] = camera.view[10] = camera.view[15] = 1.0F;
    camera.projection = camera.view;
    return camera;
}

class HeadlessSurface final : public kb::render::RenderSurface {
public:
    [[nodiscard]] std::uint32_t Width() const noexcept override { return 16U; }
    [[nodiscard]] std::uint32_t Height() const noexcept override { return 16U; }
    [[nodiscard]] void* NativeWindowHandle() const noexcept override { return nullptr; }
    [[nodiscard]] void* NativeDisplayHandle() const noexcept override { return nullptr; }
};

void TestGpuParticleProgramLoadsThroughRendererInitialization() {
    HeadlessSurface surface;
    kb::render::DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    kb::render::Renderer renderer;
    Require(renderer.Initialize(surface, &config),
        "renderer did not load the staged 21kb Particle System GPU program");
    renderer.Shutdown();
}

[[nodiscard]] std::uint64_t PackedId(const kb::render::ParticleGpuInstance& value) noexcept {
    return static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value.frameAgeIdentity[2])) |
        (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value.frameAgeIdentity[3])) << 32U);
}

void TestSortBlendFlipbookAndSoftContract() {
    const std::array particles{
        Particle(30U, 0.0F, 1.0F, 10U),
        Particle(10U, 0.0F, 3.0F, 40'000U),
        Particle(20U, 0.0F, 3.0F, 50'000U),
    };
    const auto camera = IdentityCamera();
    kb::render::ParticleRenderBatcher batcher;
    batcher.Warmup(16U);
    for (const auto sort : {kb::particles::ParticleRenderSortMode::BackToFront,
             kb::particles::ParticleRenderSortMode::FrontToBack,
             kb::particles::ParticleRenderSortMode::Distance,
             kb::particles::ParticleRenderSortMode::Age}) {
        const std::array emitters{Emitter(0U, 3U, sort)};
        const auto snapshot = Snapshot(emitters, particles);
        const auto built = batcher.Build(*snapshot, camera);
        Require(built.Succeeded() && built.batches.size() == 1U && built.instances.size() == 3U,
            "particle batch did not preserve one compatible emitter as one draw");
        if (sort == kb::particles::ParticleRenderSortMode::BackToFront ||
            sort == kb::particles::ParticleRenderSortMode::Distance) {
            Require(PackedId(built.instances[0]) == 10U && PackedId(built.instances[1]) == 20U,
                "descending particle sort or stable-id tie break changed");
        } else if (sort == kb::particles::ParticleRenderSortMode::FrontToBack) {
            Require(PackedId(built.instances[0]) == 30U,
                "front-to-back particle sort is not increasing view depth");
        } else {
            Require(PackedId(built.instances[0]) == 20U,
                "age particle sort does not place older normalized age first");
        }
    }
    const std::array additiveEmitter{Emitter(0U, 3U,
        kb::particles::ParticleRenderSortMode::Age, kb::particles::ParticleRenderBlendMode::Add)};
    const auto additive = batcher.Build(*Snapshot(additiveEmitter, particles), camera);
    Require(PackedId(additive.instances[0]) == 30U && PackedId(additive.instances[2]) == 20U,
        "additive particle batches must preserve compact snapshot order");
    Require(kb::render::ParticleSoftFade(5.05F, 5.0F) > 0.499F &&
            kb::render::ParticleSoftFade(5.05F, 5.0F) < 0.501F &&
            kb::render::ParticleSoftFade(4.0F, 5.0F) == 0.0F &&
            kb::render::ParticleSoftFade(5.2F, 5.0F) == 1.0F,
        "soft-particle fade is not the canonical 0.10 metre saturated difference");
    Require(additiveEmitter[0].FlipbookColumns() == 4U && additiveEmitter[0].FlipbookRows() == 2U,
        "flipbook grid metadata did not retain authored dimensions");
    const auto rotatedCorner = kb::render::RotateParticleCorner(
        {1.0F, 0.0F}, 1.57079632679F);
    Require(std::fabs(rotatedCorner[0]) < 0.0001F && std::fabs(rotatedCorner[1] - 1.0F) < 0.0001F,
        "per-particle rotation did not rotate the billboard plane by the authored radians");

    const std::array crossEmitterParticles{
        Particle(4U, 0.0F, 1.0F, 0U), Particle(2U, 0.0F, 4.0F, 0U),
        Particle(3U, 0.0F, 2.0F, 0U), Particle(1U, 0.0F, 4.0F, 0U)};
    std::array crossEmitters{
        Emitter(0U, 2U, kb::particles::ParticleRenderSortMode::BackToFront),
        Emitter(2U, 2U, kb::particles::ParticleRenderSortMode::BackToFront)};
    crossEmitters[1].emitterId = 33U;
    for (const auto sort : {kb::particles::ParticleRenderSortMode::BackToFront,
             kb::particles::ParticleRenderSortMode::Distance}) {
        crossEmitters[0].sort = crossEmitters[1].sort = sort;
        const auto crossBuilt = batcher.Build(*Snapshot(crossEmitters, crossEmitterParticles), camera);
        Require(crossBuilt.Succeeded() && crossBuilt.batches.size() == 1U &&
                PackedId(crossBuilt.instances[0]) == 1U && PackedId(crossBuilt.instances[1]) == 2U &&
                PackedId(crossBuilt.instances[2]) == 3U && PackedId(crossBuilt.instances[3]) == 4U,
            "compatible emitters were not globally depth/distance sorted with stable particle-id ties");
    }
    auto ageParticles = crossEmitterParticles;
    ageParticles[0].normalizedAgeUnorm = 10U;
    ageParticles[1].normalizedAgeUnorm = 30U;
    ageParticles[2].normalizedAgeUnorm = 20U;
    ageParticles[3].normalizedAgeUnorm = 40U;
    crossEmitters[0].sort = crossEmitters[1].sort = kb::particles::ParticleRenderSortMode::Age;
    const auto ageBuilt = batcher.Build(*Snapshot(crossEmitters, ageParticles), camera);
    Require(ageBuilt.Succeeded() && ageBuilt.batches.size() == 1U &&
            PackedId(ageBuilt.instances[0]) == 1U && PackedId(ageBuilt.instances[1]) == 2U &&
            PackedId(ageBuilt.instances[2]) == 3U && PackedId(ageBuilt.instances[3]) == 4U,
        "compatible emitters were not globally age sorted across emitter ranges");

    crossEmitters[0].sort = crossEmitters[1].sort = kb::particles::ParticleRenderSortMode::BackToFront;
    crossEmitters[0].output = kb::particles::ParticleRenderOutput::Trail;
    const auto mixed = batcher.Build(*Snapshot(crossEmitters, crossEmitterParticles), camera);
    Require(mixed.Succeeded() && mixed.unsupportedEmitterCount == 1U &&
            mixed.droppedParticleCount == 2U && mixed.instances.size() == 2U &&
            mixed.unsupportedEmitterRecordIndices.size() == 1U &&
            mixed.unsupportedEmitterRecordIndices[0] == 0U,
        "one unsupported emitter hid supported GPU particle batches in a mixed effect");

    // Mesh output is handled by the separate mesh-instancing path (reusing the scene mesh
    // pipeline), not this quad/billboard batcher - it must be silently excluded here, counted
    // neither as a rendered GPU batch nor as a dropped/unsupported emitter.
    auto meshMixedEmitters = crossEmitters;
    meshMixedEmitters[0].output = kb::particles::ParticleRenderOutput::Mesh;
    meshMixedEmitters[1].output = kb::particles::ParticleRenderOutput::Billboard;
    const auto meshMixed = batcher.Build(*Snapshot(meshMixedEmitters, crossEmitterParticles), camera);
    Require(meshMixed.Succeeded() && meshMixed.unsupportedEmitterCount == 0U &&
            meshMixed.droppedParticleCount == 0U && meshMixed.unsupportedEmitterRecordIndices.empty() &&
            meshMixed.batches.size() == 1U && meshMixed.instances.size() == 2U,
        "mesh output emitter was misreported as dropped/unsupported instead of silently excluded");
}

void TestCapacitySplitNoProxyGrowthTwoViewsAndNoAllocation() {
    constexpr std::uint32_t count = 100'000U;
    std::vector<kb::particles::ParticleRenderRecord> particles(count);
    for (std::uint32_t index = 0U; index < count; ++index) {
        particles[index] = Particle(index + 1U, static_cast<float>(index), 1.0F, 0U);
    }
    const std::array emitters{Emitter(0U, count, kb::particles::ParticleRenderSortMode::None)};
    const auto snapshot = Snapshot(emitters, particles);
    kb::render::RenderScene renderScene;
    renderScene.SetParticleRenderSnapshot(snapshot);
    Require(renderScene.MeshProxyCount() == 0U && renderScene.ParticleRenderSnapshot() == snapshot,
        "100k retained particles grew the mesh proxy map");

    kb::render::ParticleRenderBatcher viewportA;
    kb::render::ParticleRenderBatcher viewportB;
    viewportA.Warmup(count);
    viewportB.Warmup(count);
    const auto camera = IdentityCamera();
    auto firstA = viewportA.Build(*snapshot, camera);
    auto firstB = viewportB.Build(*snapshot, camera);
    Require(firstA.Succeeded() && firstB.Succeeded() &&
            firstA.instances.size() == count && firstB.instances.size() == count &&
            firstA.batches.size() == (count + kb::render::kParticleGpuInstancesPerDraw - 1U) /
                kb::render::kParticleGpuInstancesPerDraw &&
            snapshot->Revision() == 1U && snapshot->FixedStepIndex() == 12U,
        "two view consumers did not batch one immutable simulation revision or split only at GPU capacity");

    g_allocations.store(0U, std::memory_order_release);
    g_measureAllocations.store(true, std::memory_order_release);
    firstA = viewportA.Build(*snapshot, camera);
    g_measureAllocations.store(false, std::memory_order_release);
    Require(firstA.Succeeded() && g_allocations.load(std::memory_order_acquire) == 0U,
        "post-warm particle batch construction allocated");
}

void TestAllBlendAndOutputContracts() {
    using Blend = kb::particles::ParticleRenderBlendMode;
    for (const Blend blend : {Blend::Opaque, Blend::Alpha, Blend::Add, Blend::Multiply,
             Blend::Subtract, Blend::Premultiplied}) {
        const std::uint64_t state = kb::render::ParticleBlendState(
            blend, kb::particles::ParticleRenderDepthMode::ReadWrite);
        Require((state & BGFX_STATE_DEPTH_TEST_MASK) != 0U, "particle blend lost depth-read state");
        if (blend == Blend::Subtract) {
            Require((state & BGFX_STATE_WRITE_Z) == 0U && (state & BGFX_STATE_WRITE_A) == 0U,
                "subtractive particles must read depth, preserve destination alpha and never write depth");
            const std::uint64_t exactBlend = BGFX_STATE_BLEND_FUNC_SEPARATE(
                    BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE,
                    BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE) |
                BGFX_STATE_BLEND_EQUATION_SEPARATE(
                    BGFX_STATE_BLEND_EQUATION_REVSUB, BGFX_STATE_BLEND_EQUATION_ADD);
            Require((state & (BGFX_STATE_BLEND_MASK | BGFX_STATE_BLEND_EQUATION_MASK)) == exactBlend,
                "subtractive particles are not dstRGB-srcRGB*srcAlpha with unchanged destination alpha");
        }
    }
    for (const auto output : {kb::particles::ParticleRenderOutput::Billboard,
             kb::particles::ParticleRenderOutput::StretchedBillboard,
             kb::particles::ParticleRenderOutput::PointSprite}) {
        auto emitter = Emitter(0U, 1U, kb::particles::ParticleRenderSortMode::None);
        emitter.output = output;
        const std::array emitters{emitter};
        const std::array particles{Particle(1U, 0.0F, 1.0F, 0U)};
        kb::render::ParticleRenderBatcher batcher;
        batcher.Warmup(1U);
        Require(batcher.Build(*Snapshot(emitters, particles), IdentityCamera()).Succeeded(),
            "supported GPU particle output was rejected");
    }

    auto alignmentEmitter = Emitter(0U, 1U, kb::particles::ParticleRenderSortMode::None);
    auto particle = Particle(1U, 0.0F, 0.0F, 0U);
    const auto camera = IdentityCamera();
    alignmentEmitter.alignment = kb::particles::ParticleRenderAlignment::Velocity;
    particle.velocity = {0.0F, 0.0F, 0.0F};
    auto basis = kb::render::ResolveParticleAlignmentBasis(alignmentEmitter, particle, camera);
    Require(basis.right == std::array<float, 3>{1.0F, 0.0F, 0.0F} &&
            basis.up == std::array<float, 3>{0.0F, 1.0F, 0.0F} &&
            basis.forward == std::array<float, 3>{0.0F, 0.0F, 1.0F},
        "degenerate velocity alignment did not fall back to camera-facing at epsilon 1e-5");
    alignmentEmitter.alignment = kb::particles::ParticleRenderAlignment::WorldUp;
    basis = kb::render::ResolveParticleAlignmentBasis(alignmentEmitter, particle, camera);
    Require(std::fabs(basis.up[1] - 1.0F) < 0.0001F,
        "world-up alignment was not orthonormalized against camera forward");
    alignmentEmitter.alignment = kb::particles::ParticleRenderAlignment::Local;
    alignmentEmitter.localBasisQuaternionSnorm = {};
    basis = kb::render::ResolveParticleAlignmentBasis(alignmentEmitter, particle, camera);
    Require(basis.right == std::array<float, 3>{1.0F, 0.0F, 0.0F},
        "invalid local basis did not fall back to camera-facing");

    std::array<kb::render::TransparentDrawOrderEntry, 4U> transparentOrder{
        kb::render::TransparentDrawOrderEntry{.source = kb::render::TransparentDrawSource::Mesh,
            .depthBucket = kb::render::QuantizeTransparentViewDepth(10.0F),
            .sourceIndex = 0U, .stableTie = 9U},
        kb::render::TransparentDrawOrderEntry{.source = kb::render::TransparentDrawSource::Particle,
            .depthBucket = kb::render::QuantizeTransparentViewDepth(20.0F),
            .sourceIndex = 0U, .stableTie = 8U},
        kb::render::TransparentDrawOrderEntry{.source = kb::render::TransparentDrawSource::Mesh,
            .depthBucket = kb::render::QuantizeTransparentViewDepth(30.0F),
            .sourceIndex = 1U, .stableTie = 7U},
        kb::render::TransparentDrawOrderEntry{.source = kb::render::TransparentDrawSource::Particle,
            .unsorted = true,
            .depthBucket = kb::render::QuantizeTransparentViewDepth(40.0F),
            .sourceIndex = 1U, .stableTie = 6U},
    };
    kb::render::SortTransparentDrawOrder(transparentOrder);
    Require(transparentOrder[0].source == kb::render::TransparentDrawSource::Mesh &&
            transparentOrder[0].sourceIndex == 1U &&
            transparentOrder[1].source == kb::render::TransparentDrawSource::Particle &&
            transparentOrder[2].source == kb::render::TransparentDrawSource::Mesh &&
            transparentOrder[3].unsorted,
        "far mesh, particle batch, and near mesh did not interleave on the shared view-depth key");
}

void TestMeshBatchBuilderInstancesLodShadowAndExclusion() {
    kb::particles::ParticleRenderEmitterRecord meshEmitter = Emitter(
        0U, 3U, kb::particles::ParticleRenderSortMode::None);
    meshEmitter.output = kb::particles::ParticleRenderOutput::Mesh;
    meshEmitter.meshAssetId = 4008U;
    meshEmitter.materialAssetId = 3008U;
    meshEmitter.meshLodLevel = -2;
    meshEmitter.flags = kb::particles::ParticleRenderEmitterFlag::CastsShadow |
        kb::particles::ParticleRenderEmitterFlag::ReceivesShadow;
    meshEmitter.localBasisQuaternionSnorm = {0, 0, 0, 32'767}; // identity

    kb::particles::ParticleRenderEmitterRecord billboardEmitter = Emitter(
        3U, 1U, kb::particles::ParticleRenderSortMode::None);
    billboardEmitter.instanceId = 2U;
    billboardEmitter.emitterId = 4U;
    billboardEmitter.output = kb::particles::ParticleRenderOutput::Billboard;

    const std::array emitters{meshEmitter, billboardEmitter};
    const std::array particles{
        Particle(101U, 1.0F, 2.0F, 0U), Particle(102U, 3.0F, 4.0F, 0U),
        Particle(103U, 5.0F, 6.0F, 0U), Particle(201U, 7.0F, 8.0F, 0U)};

    kb::render::ParticleMeshBatchBuilder builder;
    builder.Warmup(16U);
    builder.Build(*Snapshot(emitters, particles));
    const auto& batches = builder.Batches();
    Require(batches.size() == 1U && batches[0].meshAssetId == 4008U && batches[0].materialAssetId == 3008U &&
            batches[0].instances.size() == 3U,
        "mesh batch builder did not produce exactly one batch for the Mesh emitter, excluding Billboard");
    const auto& instance = batches[0].instances[1];
    Require(instance.entityId == 102U && instance.meshAssetId == 4008U && instance.materialAssetId == 3008U &&
            instance.castsShadow && instance.receivesShadow && instance.lodBias == -2,
        "mesh instance did not carry identity, shadow flags, or LOD level from its emitter record");
    Require(std::fabs(instance.model[12] - 3.0F) < 0.0001F && std::fabs(instance.model[13] - 0.0F) < 0.0001F &&
            std::fabs(instance.model[14] - 4.0F) < 0.0001F && std::fabs(instance.model[0] - 1.0F) < 0.0001F &&
            std::fabs(instance.model[5] - 1.0F) < 0.0001F && std::fabs(instance.model[10] - 1.0F) < 0.0001F,
        "mesh instance model matrix did not place an identity-oriented, unit-size particle at its position");
    Require(instance.color[0] > 0.0F && instance.color[3] > 0.0F,
        "mesh instance color was not unpacked from the particle's packed color");

    kb::particles::ParticleRenderEmitterRecord soloBillboardEmitter = Emitter(
        0U, 1U, kb::particles::ParticleRenderSortMode::None);
    const std::array soloEmitters{soloBillboardEmitter};
    const std::array soloParticles{Particle(301U, 9.0F, 10.0F, 0U)};
    kb::render::ParticleMeshBatchBuilder emptyBuilder;
    emptyBuilder.Warmup(4U);
    emptyBuilder.Build(*Snapshot(soloEmitters, soloParticles));
    Require(emptyBuilder.Batches().empty(), "mesh batch builder produced a batch for a non-Mesh-only snapshot");
}

} // namespace

void* operator new(std::size_t size) {
    if (g_measureAllocations.load(std::memory_order_relaxed)) {
        g_allocations.fetch_add(1U, std::memory_order_relaxed);
    }
    if (void* memory = std::malloc(size == 0U ? 1U : size)) return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
    try {
        TestGpuParticleProgramLoadsThroughRendererInitialization();
        TestSortBlendFlipbookAndSoftContract();
        TestCapacitySplitNoProxyGrowthTwoViewsAndNoAllocation();
        TestAllBlendAndOutputContracts();
        TestMeshBatchBuilderInstancesLodShadowAndExclusion();
        std::cout << "21kb Particle System GPU renderer tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        g_measureAllocations.store(false, std::memory_order_release);
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
