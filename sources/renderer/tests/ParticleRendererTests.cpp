#include "engine/particles/ParticleRenderSnapshot.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/particles/ParticleMeshBatchBuilder.hpp"
#include "kb/render/particles/ParticleRenderBatcher.hpp"
#include "kb/render/particles/ParticleStripGeometryBuilder.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneParticleRenderSynchronizer.hpp"
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
    crossEmitters[0].output = kb::particles::ParticleRenderOutput::Volumetric;
    const auto mixed = batcher.Build(*Snapshot(crossEmitters, crossEmitterParticles), camera);
    Require(mixed.Succeeded() && mixed.unsupportedEmitterCount == 0U &&
            mixed.droppedParticleCount == 0U && mixed.instances.size() == 4U &&
            mixed.batches.size() == 2U && mixed.unsupportedEmitterRecordIndices.empty(),
        "volumetric particles were not retained as a supported depth-aware GPU batch");
    crossEmitters[1].output = kb::particles::ParticleRenderOutput::Volumetric;
    crossEmitters[1].volumetricHighQualitySteps = 32U;
    const auto distinctVolumetric = batcher.Build(*Snapshot(crossEmitters, crossEmitterParticles), camera);
    Require(distinctVolumetric.Succeeded() && distinctVolumetric.batches.size() == 2U &&
            distinctVolumetric.instances.size() == 4U,
        "volumetric emitters with different raymarch quality merged into one draw");

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
        // The scene colour alpha is the share of the opaque surface still visible (the TAA reactive mask):
        // particles covering it multiply it by 1 - their opacity (ZERO, INV_SRC_ALPHA), multiplying
        // particles only tint it (ZERO, ONE).
        const auto expectBlend = [state](std::uint64_t expected, const char* message) {
            Require((state & BGFX_STATE_BLEND_MASK) == expected, message);
        };
        if (blend == Blend::Alpha) {
            expectBlend(BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                    BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_ALPHA),
                "alpha particles must blend by alpha and hide the surface behind them by their opacity");
        } else if (blend == Blend::Add) {
            expectBlend(BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE,
                    BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_ALPHA),
                "additive particles must add and hide the surface behind them by their opacity");
        } else if (blend == Blend::Premultiplied) {
            expectBlend(BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                    BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_ALPHA),
                "premultiplied particles must composite and hide the surface behind them by their opacity");
        } else if (blend == Blend::Multiply) {
            expectBlend(BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ZERO,
                    BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE),
                "multiplying particles must tint and keep the surface behind them visible");
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
    particle.velocity = {0.0F, 1.0F, 1.0F};
    const auto firstVelocityBasis =
        kb::render::ResolveParticleAlignmentBasis(alignmentEmitter, particle, camera);
    auto rotatedCamera = IdentityCamera();
    rotatedCamera.view = {
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        -1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
    const auto rotatedVelocityBasis =
        kb::render::ResolveParticleAlignmentBasis(
            alignmentEmitter, particle, rotatedCamera);
    Require(firstVelocityBasis.up == rotatedVelocityBasis.up &&
            std::fabs(firstVelocityBasis.up[1] - 0.70710677F) < 0.0001F &&
            std::fabs(firstVelocityBasis.up[2] - 0.70710677F) < 0.0001F,
        "velocity-aligned particle axis changed with the camera instead of remaining in world space");

    const std::array worldParticles{particle};
    const std::array worldEmitters{alignmentEmitter};
    const auto worldSnapshot = Snapshot(worldEmitters, worldParticles);
    kb::render::ParticleRenderBatcher worldBatcher;
    worldBatcher.Warmup(1U);
    const auto firstWorldBuild = worldBatcher.Build(*worldSnapshot, camera);
    Require(firstWorldBuild.Succeeded() && firstWorldBuild.instances.size() == 1U,
        "world-space particle regression setup did not build an instance");
    const auto firstWorldCenter = firstWorldBuild.instances.front().positionSize;
    const auto rotatedWorldBuild = worldBatcher.Build(*worldSnapshot, rotatedCamera);
    Require(rotatedWorldBuild.Succeeded() && rotatedWorldBuild.instances.size() == 1U &&
            rotatedWorldBuild.instances.front().positionSize == firstWorldCenter,
        "particle instance center changed when only the camera changed");

    alignmentEmitter.alignment = kb::particles::ParticleRenderAlignment::WorldUp;
    basis = kb::render::ResolveParticleAlignmentBasis(alignmentEmitter, particle, camera);
    const auto rotatedWorldUpBasis =
        kb::render::ResolveParticleAlignmentBasis(
            alignmentEmitter, particle, rotatedCamera);
    Require(basis.up == std::array<float, 3>{0.0F, 1.0F, 0.0F} &&
            rotatedWorldUpBasis.up == basis.up,
        "world-up particle axis changed with the camera");
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

// A translucent mesh emitter is drawn back to front (or by its other sort mode), an opaque one keeps the snapshot
// order; the spin of a particle turns its instance about the emitter's Z axis.
void TestMeshBatchBuilderOrdersTranslucentAndSpins() {
    kb::render::RenderResourceRegistry resources;
    kb::render::RenderMaterialDesc translucentDesc{};
    translucentDesc.alphaMode = kb::render::RenderMaterialAlphaMode::Blend;
    kb::render::SceneRenderResourceMap bindings;
    bindings.BindMaterial(3010U, resources.RegisterMaterial(translucentDesc));
    bindings.BindMaterial(3011U, resources.RegisterMaterial(kb::render::RenderMaterialDesc{}));

    const auto build = [&](std::uint64_t materialAssetId, kb::particles::ParticleRenderSortMode sort) {
        kb::particles::ParticleRenderEmitterRecord emitter = Emitter(0U, 3U, sort);
        emitter.output = kb::particles::ParticleRenderOutput::Mesh;
        emitter.meshAssetId = 4010U;
        emitter.materialAssetId = materialAssetId;
        emitter.localBasisQuaternionSnorm = {0, 0, 0, 32'767};
        std::array<kb::particles::ParticleRenderRecord, 3U> particles{Particle(1U, 0.0F, 1.0F, 30000U), Particle(2U, 0.0F, 3.0F, 10000U), Particle(3U, 0.0F, 2.0F, 50000U)};
        particles[0].rotationRadians = 1.5707964F;
        kb::render::SceneRenderCamera camera{};
        camera.view = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
        const std::array emitters{emitter};
        kb::render::ParticleMeshBatchBuilder builder;
        builder.Build(*Snapshot(emitters, particles), &camera, &resources, &bindings);
        std::vector<std::uint64_t> ids;
        std::array<float, 16> turned{};
        for (const auto& batch : builder.Batches()) {
            for (const auto& instance : batch.instances) {
                ids.push_back(instance.entityId);
                if (instance.entityId == 1U) turned = instance.model;
            }
        }
        return std::pair{ids, turned};
    };
    using Sort = kb::particles::ParticleRenderSortMode;
    const auto backToFront = build(3010U, Sort::BackToFront);
    Require(backToFront.first == std::vector<std::uint64_t>{2U, 3U, 1U}, "a translucent mesh emitter was not drawn back to front");
    Require(build(3010U, Sort::FrontToBack).first == std::vector<std::uint64_t>{1U, 3U, 2U}, "front to back order is wrong");
    Require(build(3010U, Sort::Age).first == std::vector<std::uint64_t>{3U, 1U, 2U}, "age order is wrong (oldest first)");
    Require(build(3011U, Sort::BackToFront).first == std::vector<std::uint64_t>{1U, 2U, 3U}, "an opaque mesh emitter must keep the snapshot order");
    Require(build(3010U, Sort::None).first == std::vector<std::uint64_t>{1U, 2U, 3U}, "sort mode None must keep the snapshot order");
    // A mesh that follows its velocity (here +x): its Y axis is the velocity, X = (0, 0, -1) and Z = (0, -1, 0); a particle
    // that stands still keeps the emitter's basis.
    {
        kb::particles::ParticleRenderEmitterRecord emitter = Emitter(0U, 2U, Sort::None);
        emitter.output = kb::particles::ParticleRenderOutput::Mesh;
        emitter.alignment = kb::particles::ParticleRenderAlignment::Velocity;
        emitter.meshAssetId = 4010U;
        emitter.localBasisQuaternionSnorm = {0, 0, 0, 32'767};
        std::array<kb::particles::ParticleRenderRecord, 2U> particles{Particle(1U, 0.0F, 1.0F, 0U), Particle(2U, 0.0F, 2.0F, 0U)};
        particles[1].velocity = {};
        const std::array emitters{emitter};
        kb::render::ParticleMeshBatchBuilder builder;
        builder.Build(*Snapshot(emitters, particles));
        const auto& moving = builder.Batches().front().instances[0].model;
        const auto& still = builder.Batches().front().instances[1].model;
        Require(std::fabs(moving[4] - 1.0F) < 0.001F && std::fabs(moving[2] + 1.0F) < 0.001F && std::fabs(moving[9] + 1.0F) < 0.001F,
                "a mesh following its +x velocity did not get Y = velocity, X = -Z and Z = -Y");
        Require(std::fabs(still[0] - 1.0F) < 0.001F && std::fabs(still[5] - 1.0F) < 0.001F && std::fabs(still[10] - 1.0F) < 0.001F,
                "a standing particle must keep the emitter's basis");
    }
    // A turn about Y alone: the Z axis becomes X (and the X axis becomes -Z).
    {
        kb::particles::ParticleRenderEmitterRecord emitter = Emitter(0U, 1U, Sort::None);
        emitter.output = kb::particles::ParticleRenderOutput::Mesh;
        emitter.meshAssetId = 4010U;
        emitter.localBasisQuaternionSnorm = {0, 0, 0, 32'767};
        std::array<kb::particles::ParticleRenderRecord, 1U> particles{Particle(1U, 0.0F, 1.0F, 0U)};
        particles[0].rotationYSnorm = kb::particles::PackParticleAngle(1.5707964F);
        const std::array emitters{emitter};
        kb::render::ParticleMeshBatchBuilder builder;
        builder.Build(*Snapshot(emitters, particles));
        const auto& model = builder.Batches().front().instances.front().model;
        Require(std::fabs(model[2] + 1.0F) < 0.001F && std::fabs(model[8] - 1.0F) < 0.001F && std::fabs(model[0]) < 0.001F,
                "a turn about Y did not carry the mesh X axis to -Z and the Z axis to X");
    }
    // A quarter turn about Z: the X axis becomes Y and the Y axis becomes -X.
    const std::array<float, 16>& turned = backToFront.second;
    Require(std::fabs(turned[0]) < 0.001F && std::fabs(turned[1] - 1.0F) < 0.001F && std::fabs(turned[4] + 1.0F) < 0.001F &&
                std::fabs(turned[5]) < 0.001F && std::fabs(turned[10] - 1.0F) < 0.001F,
            "the spin of a particle did not turn its mesh instance about the Z axis");
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

    kb::render::RenderMeshResource meshResource{};
    meshResource.indexCount = 12U;
    meshResource.bounds = kb::render::RenderBoundsSphere{
        .center = {0.0F, 0.0F, 0.0F}, .radius = 0.1F};
    meshResource.sections = {
        kb::render::RenderMeshSection{
            .indexStart = 0U, .indexCount = 6U, .bounds = meshResource.bounds, .lodLevel = 0U},
        kb::render::RenderMeshSection{
            .indexStart = 6U, .indexCount = 6U, .bounds = meshResource.bounds, .lodLevel = 1U},
    };
    meshResource.lods = {
        kb::render::RenderMeshLodDesc{.firstSection = 0U, .sectionCount = 1U, .minScreenCoverage = 0.5F},
        kb::render::RenderMeshLodDesc{.firstSection = 1U, .sectionCount = 1U, .minScreenCoverage = 0.0F},
    };
    auto lodEmitter = meshEmitter;
    lodEmitter.meshLodLevel = 1;
    const std::array lodEmitters{lodEmitter};
    const std::array lodParticles{
        Particle(101U, 0.1F, 0.2F, 0U), Particle(102U, 0.2F, 0.4F, 0U),
        Particle(103U, 0.3F, 0.6F, 0U)};
    kb::render::ParticleMeshBatchBuilder lodBuilder;
    lodBuilder.Warmup(3U);
    lodBuilder.Build(*Snapshot(lodEmitters, lodParticles));
    const auto& lodBatches = lodBuilder.Batches();
    kb::render::SceneRenderCamera meshCamera = IdentityCamera();
    const kb::render::MeshPipelineBuildResult meshCommands = kb::render::MeshPipelineProcessor::Build(
        kb::render::MeshPipelineBuildDesc{
            .pass = kb::render::MeshPassType::BaseOpaque,
            .meshBatches = &lodBatches,
            .resolvedMeshResource = &meshResource,
            .camera = &meshCamera,
            .resourceValidation = kb::render::MeshPipelineResourceValidation::Skip,
        });
    Require(meshCommands.commands.size() == 1U && meshCommands.commands[0].lodLevel == 1U &&
            meshCommands.commands[0].instances.size() == 3U,
        "mesh particles did not resolve to one instanced draw command for their selected LOD");
    kb::render::SceneRenderSubmitStats meshSubmitStats = meshCommands.stats;
    kb::render::MeshPipelineProcessor::CountCommandsAsSubmitted(meshSubmitStats, meshCommands.commands);
    Require(meshSubmitStats.submittedDrawCallCount == 1U && meshSubmitStats.submittedMeshCount == 3U,
        "mesh particle draw accounting did not preserve one draw for three instances");
    const kb::render::MeshPipelineBuildResult shadowCommands = kb::render::MeshPipelineProcessor::Build(
        kb::render::MeshPipelineBuildDesc{
            .pass = kb::render::MeshPassType::ShadowDepth,
            .meshBatches = &lodBatches,
            .resolvedMeshResource = &meshResource,
            .camera = &meshCamera,
            .resourceValidation = kb::render::MeshPipelineResourceValidation::Skip,
        });
    Require(shadowCommands.commands.size() == 1U && shadowCommands.commands[0].instances.size() == 3U,
        "shadow-casting mesh particles were not retained by the shadow pass");

    kb::particles::ParticleRenderEmitterRecord soloBillboardEmitter = Emitter(
        0U, 1U, kb::particles::ParticleRenderSortMode::None);
    const std::array soloEmitters{soloBillboardEmitter};
    const std::array soloParticles{Particle(301U, 9.0F, 10.0F, 0U)};
    kb::render::ParticleMeshBatchBuilder emptyBuilder;
    emptyBuilder.Warmup(4U);
    emptyBuilder.Build(*Snapshot(soloEmitters, soloParticles));
    Require(emptyBuilder.Batches().empty(), "mesh batch builder produced a batch for a non-Mesh-only snapshot");
}

void TestStripGeometryHistoryOrderAndFixedStepDeterminism() {
    auto emitter = Emitter(0U, 2U, kb::particles::ParticleRenderSortMode::None);
    emitter.output = kb::particles::ParticleRenderOutput::Trail;
    emitter.trailSampleIntervalSeconds = 1.0F / 60.0F;
    emitter.trailMinimumDistance = 0.0F;
    emitter.trailMaxSamplesPerParticle = 4U;
    emitter.trailWidth = 0.5F;
    std::array trailParticles{Particle(11U, 0.0F, 0.0F, 0U), Particle(12U, 1.0F, 0.0F, 0U)};
    kb::particles::ParticleRenderSnapshotChannel channel;
    Require(channel.Warmup(131U).Succeeded(), "strip snapshot warmup failed");
    const auto publish = [&](std::uint64_t revision, std::uint64_t step) {
        Require(channel.Publish(1U, {.revision = revision, .fixedStepIndex = step,
                    .emitters = std::span{&emitter, 1U}, .particles = trailParticles}).Succeeded(),
            "strip snapshot publication failed");
        return channel.Read();
    };
    kb::render::ParticleStripGeometryBuilder builder;
    builder.Warmup();
    const auto first = publish(1U, 1U);
    Require(builder.Build(*first, IdentityCamera()).Succeeded(), "first trail geometry build failed");
    std::swap(trailParticles[0], trailParticles[1]);
    trailParticles[0].position.x += 1.0F;
    trailParticles[1].position.x += 1.0F;
    const auto second = publish(2U, 2U);
    const auto trails = builder.Build(*second, IdentityCamera());
    Require(trails.Succeeded() && trails.draws.size() == 1U && trails.indices.size() == 12U,
        "trail history was not retained by particle identity across compact-order changes");
    kb::particles::ParticleRenderSnapshotChannel restartedTrailChannel;
    Require(restartedTrailChannel.Warmup(131U).Succeeded(), "restarted trail snapshot warmup failed");
    Require(restartedTrailChannel.Publish(2U, {.revision = 1U, .fixedStepIndex = 1U,
                .emitters = std::span{&emitter, 1U}, .particles = trailParticles}).Succeeded(),
        "restarted trail snapshot publication failed");
    Require(builder.Build(*restartedTrailChannel.Read(), IdentityCamera()).Succeeded() &&
            builder.Build(*restartedTrailChannel.Read(), IdentityCamera()).indices.empty(),
        "trail history survived a backend epoch change");

    emitter.output = kb::particles::ParticleRenderOutput::Ribbon;
    emitter.ribbonMaxSegments = 8U;
    emitter.ribbonWidth = 0.25F;
    emitter.ribbonBreakOnDeath = true;
    trailParticles[0].spawnOrdinal = 5U;
    trailParticles[1].spawnOrdinal = 7U;
    const auto ribbonSnapshot = publish(3U, 3U);
    const auto ribbon = builder.Build(*ribbonSnapshot, IdentityCamera());
    Require(ribbon.Succeeded() && ribbon.draws.empty(),
        "ribbon connected a discontinuity after an ordered particle was removed");

    emitter.output = kb::particles::ParticleRenderOutput::Beam;
    emitter.particleCount = 0U;
    emitter.liveParticleCount = 0U;
    emitter.outputOrigin = {0.0F, 0.0F, 0.0F};
    emitter.beamEnd = {0.0F, 4.0F, 0.0F};
    emitter.beamSegments = 4U;
    emitter.beamNoiseAmplitude = 0.5F;
    emitter.beamNoiseFrequency = 1.25F;
    kb::particles::ParticleRenderSnapshotChannel beamChannel;
    Require(beamChannel.Warmup(132U).Succeeded(), "beam snapshot warmup failed");
    Require(beamChannel.Publish(1U, {.revision = 1U, .fixedStepIndex = 77U,
                .emitters = std::span{&emitter, 1U}, .particles = std::span<const kb::particles::ParticleRenderRecord>{}}).Succeeded(),
        "beam snapshot publication failed");
    const auto beamSnapshot = beamChannel.Read();
    const auto beamA = builder.Build(*beamSnapshot, IdentityCamera());
    // One strip: two vertices per point (5 points, 4 segments). The ends stay put, the middle is pushed
    // across the beam by the noise.
    const auto centre = [](const auto& built, std::size_t point) {
        return std::array<float, 2>{(built.vertices[2U * point].x + built.vertices[2U * point + 1U].x) * 0.5F,
                                    (built.vertices[2U * point].y + built.vertices[2U * point + 1U].y) * 0.5F};
    };
    const float beamStartX = beamA.vertices[0].x;
    const float beamEndX = beamA.vertices[8].x;
    const auto beamB = builder.Build(*beamSnapshot, IdentityCamera());
    Require(beamA.Succeeded() && beamA.vertices.size() == 10U && beamA.indices.size() == 24U &&
            beamA.vertices[4].x == beamB.vertices[4].x && beamA.vertices[4].y == beamB.vertices[4].y,
        "beam geometry changed without a fixed-step advance");
    Require(std::fabs(centre(beamA, 0U)[0]) < 0.0001F && std::fabs(centre(beamA, 0U)[1]) < 0.0001F &&
            std::fabs(centre(beamA, 4U)[1] - 4.0F) < 0.0001F && std::fabs(centre(beamA, 2U)[0]) > 0.0001F,
        "beam noise must bend the beam across itself and keep both ends in place");
    // The first segment ends on the vertices the second starts from (no gap or overlap at the joint).
    Require(beamA.indices[2] == beamA.indices[7] && beamA.indices[5] == beamA.indices[6],
        "beam joints must share their vertices");
    auto crossingCamera = IdentityCamera();
    crossingCamera.view[0] = -1.0F;
    crossingCamera.view[10] = -1.0F;
    const auto crossed = builder.Build(*beamSnapshot, crossingCamera);
    Require(crossed.Succeeded() && crossed.indices.size() == beamA.indices.size() &&
            std::fabs(crossed.vertices[0].x + beamStartX) < 0.0001F && crossed.vertices[0].x != beamStartX,
        "camera crossing did not rebuild the strip-facing geometry");
    auto movedEmitter = emitter;
    movedEmitter.beamEnd.x = 0.5F;
    const std::array movedEmitters{movedEmitter};
    Require(beamChannel.Publish(1U, {.revision = 2U, .fixedStepIndex = 77U,
                .emitters = movedEmitters, .particles = std::span<const kb::particles::ParticleRenderRecord>{}}).Succeeded(),
        "moved beam snapshot publication failed");
    const auto moved = builder.Build(*beamChannel.Read(), IdentityCamera());
    Require(moved.Succeeded() && moved.vertices[8].x != beamEndX,
        "beam endpoint motion did not rebuild the dynamic strip geometry");
}

void TestStripGeometryCapacityIsHardAndDiagnostic() {
    constexpr std::uint32_t segmentsPerEmitter = 256U;
    // A beam strip of n segments takes 2 (n + 1) vertices and 6 n indices: the index budget fills first.
    constexpr std::uint32_t emitterCount =
        kb::render::kParticleStripIndexBudget / (segmentsPerEmitter * 6U) + 1U;
    std::vector<kb::particles::ParticleRenderEmitterRecord> emitters;
    emitters.reserve(emitterCount);
    for (std::uint32_t index = 0U; index < emitterCount; ++index) {
        auto emitter = Emitter(0U, 0U, kb::particles::ParticleRenderSortMode::None);
        emitter.instanceId = index + 1U;
        emitter.emitterId = index + 1U;
        emitter.output = kb::particles::ParticleRenderOutput::Beam;
        emitter.outputOrigin = {static_cast<float>(index), 0.0F, 0.0F};
        emitter.beamEnd = {static_cast<float>(index), 1.0F, 0.0F};
        emitter.beamSegments = segmentsPerEmitter;
        emitters.push_back(emitter);
    }

    kb::particles::ParticleRenderSnapshotChannel channel;
    Require(channel.Warmup(133U).Succeeded(), "strip capacity snapshot warmup failed");
    Require(channel.Publish(1U, {.revision = 1U, .fixedStepIndex = 1U,
                .emitters = emitters, .particles = std::span<const kb::particles::ParticleRenderRecord>{}}).Succeeded(),
        "strip capacity snapshot publication failed");
    kb::render::ParticleStripGeometryBuilder builder;
    builder.Warmup();
    const auto built = builder.Build(*channel.Read(), IdentityCamera());
    Require(built.status == kb::render::ParticleStripBuildStatus::CapacityExceeded &&
            built.vertices.size() == (emitterCount - 1U) * 2U * (segmentsPerEmitter + 1U) &&
            built.indices.size() == kb::render::kParticleStripIndexBudget &&
            built.droppedSegmentCount == segmentsPerEmitter &&
            built.draws.size() == emitterCount - 1U,
        "strip geometry exceeded its renderer-owned buffers without precise capacity diagnostics");
}

void TestTrailHistoryCapacityIsHardAndDiagnostic() {
    const std::uint32_t particleCount = kb::render::kParticleTrailHistoryBudget + 1U;
    std::vector<kb::particles::ParticleRenderRecord> particles;
    particles.reserve(particleCount);
    for (std::uint32_t index = 0U; index < particleCount; ++index) {
        auto particle = Particle(index + 1U, static_cast<float>(index), 0.0F, 0U);
        particles.push_back(particle);
    }
    auto emitter = Emitter(0U, particleCount, kb::particles::ParticleRenderSortMode::None);
    emitter.output = kb::particles::ParticleRenderOutput::Trail;
    emitter.trailMaxSamplesPerParticle = 2U;
    emitter.trailWidth = 0.25F;
    kb::particles::ParticleRenderSnapshotChannel channel;
    Require(channel.Warmup(134U).Succeeded(), "trail capacity snapshot warmup failed");
    Require(channel.Publish(1U, {.revision = 1U, .fixedStepIndex = 1U,
                .emitters = std::span{&emitter, 1U}, .particles = particles}).Succeeded(),
        "trail capacity first snapshot publication failed");
    kb::render::ParticleStripGeometryBuilder builder;
    builder.Warmup();
    Require(builder.Build(*channel.Read(), IdentityCamera()).status == kb::render::ParticleStripBuildStatus::CapacityExceeded,
        "trail history budget did not report the first rejected particle");
    for (auto& particle : particles) particle.position.y += 1.0F;
    Require(channel.Publish(1U, {.revision = 2U, .fixedStepIndex = 2U,
                .emitters = std::span{&emitter, 1U}, .particles = particles}).Succeeded(),
        "trail capacity second snapshot publication failed");
    const auto built = builder.Build(*channel.Read(), IdentityCamera());
    Require(built.status == kb::render::ParticleStripBuildStatus::CapacityExceeded &&
            built.vertices.size() == kb::render::kParticleStripVertexBudget &&
            built.indices.size() == kb::render::kParticleStripIndexBudget && built.droppedSegmentCount == 1U,
        "trail history ring exceeded its hard limit without an exact diagnostic");
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

// Strips carry the colour of their points (times the emitter's colour intensity) and a texture coordinate
// running along the whole strip; a trail fades out toward its oldest sample.
void TestStripColourIntensityAndTextureCoordinates() {
    auto emitter = Emitter(0U, 3U, kb::particles::ParticleRenderSortMode::None);
    emitter.output = kb::particles::ParticleRenderOutput::Ribbon;
    emitter.ribbonMaxSegments = 8U;
    emitter.ribbonWidth = 0.25F;
    emitter.ribbonBreakOnDeath = true;
    emitter.colorIntensity = 2.0F;
    std::array particles{Particle(21U, 0.0F, 0.0F, 0U), Particle(22U, 1.0F, 0.0F, 0U), Particle(23U, 2.0F, 0.0F, 0U)};
    particles[0].packedColor = 0xFF0000FFU; // red, opaque
    particles[1].packedColor = 0xFF00FF00U; // green
    particles[2].packedColor = 0x80FF0000U; // blue, half transparent
    for (std::uint32_t index = 0U; index < particles.size(); ++index) particles[index].spawnOrdinal = index;
    kb::render::ParticleStripGeometryBuilder builder;
    builder.Warmup();
    const auto ribbonSnapshot = Snapshot(std::span{&emitter, 1U}, particles);
    const auto ribbon = builder.Build(*ribbonSnapshot, IdentityCamera());
    // Two segments of one strip: three points, two vertices each, shared at the joint.
    Require(ribbon.Succeeded() && ribbon.vertices.size() == 6U && ribbon.indices.size() == 12U &&
            ribbon.draws.size() == 1U && ribbon.draws[0].textureAtlasAssetId == 6U,
        "a three-point ribbon did not build two textured segments");
    const auto& v = ribbon.vertices;
    Require(v[0].u == 0.0F && v[2].u == 0.5F && v[4].u == 1.0F && v[0].v == 0.0F && v[1].v == 1.0F,
        "the ribbon's texture coordinate did not run along the whole strip");
    Require(v[0].r == 2.0F && v[0].g == 0.0F && v[2].g == 2.0F && v[4].b == 2.0F &&
            std::abs(v[4].a - 128.0F / 255.0F) < 0.0001F && v[0].a == 1.0F,
        "the ribbon's points did not keep their colour times the colour intensity and their alpha");

    auto trailEmitter = Emitter(0U, 1U, kb::particles::ParticleRenderSortMode::None);
    trailEmitter.output = kb::particles::ParticleRenderOutput::Trail;
    trailEmitter.trailSampleIntervalSeconds = 1.0F / 60.0F;
    trailEmitter.trailMinimumDistance = 0.0F;
    trailEmitter.trailMaxSamplesPerParticle = 3U;
    trailEmitter.trailWidth = 0.5F;
    std::array head{Particle(31U, 0.0F, 0.0F, 0U)};
    head[0].packedColor = 0xFFFFFFFFU;
    kb::particles::ParticleRenderSnapshotChannel channel;
    Require(channel.Warmup(141U).Succeeded(), "trail fade snapshot warmup failed");
    kb::render::ParticleStripGeometryBuilder trailBuilder;
    trailBuilder.Warmup();
    kb::render::ParticleStripBuildResult trail{};
    for (std::uint64_t step = 1U; step <= 3U; ++step) {
        head[0].position.x = static_cast<float>(step);
        Require(channel.Publish(1U, {.revision = step, .fixedStepIndex = step,
                    .emitters = std::span{&trailEmitter, 1U}, .particles = head}).Succeeded(),
            "trail fade snapshot publication failed");
        trail = trailBuilder.Build(*channel.Read(), IdentityCamera());
    }
    Require(trail.Succeeded() && trail.vertices.size() == 6U && trail.vertices[0].a == 0.0F &&
            std::abs(trail.vertices[2].a - 0.5F) < 0.0001F && trail.vertices[4].a == 1.0F && trail.vertices[4].r == 1.0F,
        "the trail did not take its particle's colour fading out toward the oldest sample");
}

// Billboards and mesh particles draw their colour times the emitter's colour intensity, alpha unchanged.
void TestColourIntensityReachesBillboardsAndMeshes() {
    auto emitter = Emitter(0U, 1U, kb::particles::ParticleRenderSortMode::None, kb::particles::ParticleRenderBlendMode::Add);
    emitter.colorIntensity = 3.0F;
    std::array particles{Particle(41U, 0.0F, 0.0F, 0U)};
    particles[0].packedColor = 0x80FF0000U;
    const auto snapshot = Snapshot(std::span{&emitter, 1U}, particles);
    kb::render::ParticleRenderBatcher batcher;
    batcher.Warmup(8U);
    const auto built = batcher.Build(*snapshot, IdentityCamera());
    Require(built.Succeeded() && built.instances.size() == 1U && built.instances[0].color[2] == 3.0F &&
            std::abs(built.instances[0].color[3] - 128.0F / 255.0F) < 0.0001F,
        "a billboard's colour was not multiplied by its emitter's colour intensity");

    auto meshEmitter = emitter;
    meshEmitter.output = kb::particles::ParticleRenderOutput::Mesh;
    meshEmitter.meshAssetId = 77U;
    const auto meshSnapshot = Snapshot(std::span{&meshEmitter, 1U}, particles);
    kb::render::ParticleMeshBatchBuilder meshes;
    meshes.Warmup(8U);
    const kb::render::SceneRenderCamera camera = IdentityCamera();
    meshes.Build(*meshSnapshot, &camera);
    Require(meshes.Batches().size() == 1U && meshes.Batches()[0].instances.size() == 1U &&
            meshes.Batches()[0].instances[0].color[2] == 3.0F,
        "a mesh particle's colour was not multiplied by its emitter's colour intensity");
}

// The lit particles of an emitter become point lights of their colour (alpha scales the intensity), and
// lights of particles no longer lit go away.
void TestParticleLightsFollowTheSnapshot() {
    auto emitter = Emitter(0U, 3U, kb::particles::ParticleRenderSortMode::None, kb::particles::ParticleRenderBlendMode::Add);
    emitter.lightMaxCount = 2U;
    emitter.lightRadius = 3.0F;
    emitter.lightIntensity = 2.0F;
    std::array particles{Particle(51U, 1.0F, 0.0F, 0U), Particle(52U, 2.0F, 0.0F, 0U), Particle(53U, 3.0F, 0.0F, 0U)};
    particles[0].packedColor = 0x8000FF00U; // green, half alpha
    kb::render::RenderScene renderScene;
    std::vector<std::uint64_t> lightIds;
    kb::render::SyncParticleLights(Snapshot(std::span{&emitter, 1U}, particles).get(), renderScene, lightIds);
    Require(lightIds.size() == 2U && renderScene.LightProxyCount() == 2U, "a particle light count other than lightMaxCount was made");
    const kb::render::LightRenderProxy* light = renderScene.FindLightByEntity(lightIds[0]);
    Require(light != nullptr && light->desc.kind == kb::render::RenderLightKind::Point && light->desc.position[0] == 1.0F &&
            light->desc.color[1] == 1.0F && light->desc.color[0] == 0.0F && light->desc.range == 3.0F &&
            std::abs(light->desc.intensity - 2.0F * 128.0F / 255.0F) < 0.0001F && !light->desc.castsShadow,
        "a particle light did not take its particle's position, colour, alpha and the emitter's radius and intensity");
    emitter.lightMaxCount = 0U;
    kb::render::SyncParticleLights(Snapshot(std::span{&emitter, 1U}, particles).get(), renderScene, lightIds);
    Require(lightIds.empty() && renderScene.LightProxyCount() == 0U, "the lights of particles no longer lit stayed in the scene");
}

int main() {
    try {
        TestGpuParticleProgramLoadsThroughRendererInitialization();
        TestSortBlendFlipbookAndSoftContract();
        TestCapacitySplitNoProxyGrowthTwoViewsAndNoAllocation();
        TestAllBlendAndOutputContracts();
        TestMeshBatchBuilderInstancesLodShadowAndExclusion();
        TestMeshBatchBuilderOrdersTranslucentAndSpins();
        TestStripGeometryHistoryOrderAndFixedStepDeterminism();
        TestStripGeometryCapacityIsHardAndDiagnostic();
        TestTrailHistoryCapacityIsHardAndDiagnostic();
        TestStripColourIntensityAndTextureCoordinates();
        TestColourIntensityReachesBillboardsAndMeshes();
        TestParticleLightsFollowTheSnapshot();
        std::cout << "21kb Particle System GPU renderer tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        g_measureAllocations.store(false, std::memory_order_release);
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
