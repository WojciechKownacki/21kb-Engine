#include "RendererTestSupport.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/ParticleEffectAssetLoader.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/ShaderLoader.hpp"
#include "kb/render/particles/ParticleGpuEmitterSimulation.hpp"
#include "kb/render/particles/ParticleGpuReadback.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <limits>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace kb::render::tests {
namespace {
#if defined(_WIN32) && defined(KB_21KB_PARTICLE_PLUGIN_PATH)
constexpr float kStep = kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds;
using Motion = kb::particles::ParticleGpuMotionType;
using Module = kb::scene::ParticleModuleType;

class MotionSurface final : public RenderSurface {
public:
    MotionSurface() : window_(CreateWindowExW(0U, L"STATIC", L"Particle compute readback", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)) {}
    ~MotionSurface() override { if (window_ != nullptr) DestroyWindow(window_); }
    MotionSurface(const MotionSurface&) = delete;
    MotionSurface& operator=(const MotionSurface&) = delete;
    std::uint32_t Width() const noexcept override { return 64U; }
    std::uint32_t Height() const noexcept override { return 64U; }
    void* NativeWindowHandle() const noexcept override { return window_; }
    void* NativeDisplayHandle() const noexcept override { return nullptr; }
private:
    HWND window_ = nullptr;
};

class MotionReadback final {
public:
    MotionReadback() {
        program_ = ShaderLoader::LoadComputeProgram("cs_particle_gpu_inspect.sc");
        uniform_ = bgfx::createUniform("u_particleInspect", bgfx::UniformType::Vec4);
        output_ = bgfx::createTexture2D(8U, 1U, false, 1U, bgfx::TextureFormat::RGBA32F, BGFX_TEXTURE_COMPUTE_WRITE);
        readback_ = bgfx::createTexture2D(8U, 1U, false, 1U, bgfx::TextureFormat::RGBA32F,
            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        Require(bgfx::isValid(program_) && bgfx::isValid(uniform_) && bgfx::isValid(output_) && bgfx::isValid(readback_),
            "Particle motion inspection resources unavailable");
    }
    ~MotionReadback() {
        bgfx::destroy(readback_); bgfx::destroy(output_); bgfx::destroy(uniform_); bgfx::destroy(program_);
    }
    MotionReadback(const MotionReadback&) = delete;
    MotionReadback& operator=(const MotionReadback&) = delete;
    std::array<float, 32> Read(const ParticleGpuEmitterSimulation::Draw& draw) {
        const std::array<float, 4> count{1.0F, 0.0F, 0.0F, 0.0F};
        bgfx::setBuffer(0U, draw.instances, bgfx::Access::Read);
        bgfx::setImage(1U, output_, 0U, bgfx::Access::Write, bgfx::TextureFormat::RGBA32F);
        bgfx::setUniform(uniform_, count.data());
        bgfx::dispatch(10U, program_, 1U, 1U, 1U);
        bgfx::blit(11U, bgfx::TextureRegion{.handle = readback_},
            bgfx::TextureRegion{.handle = output_, .width = 8U, .height = 1U});
        std::array<float, 32> result{};
        const auto ready = bgfx::read(bgfx::TextureRegion{.handle = readback_}, result.data());
        auto frame = bgfx::frame();
        for (auto guard = 0U; frame < ready && guard < 8U; ++guard) frame = bgfx::frame();
        Require(frame >= ready, "Particle motion GPU readback timed out");
        Require(native_.Read(draw, 1U) && native_.Records().size() == 1U, "Native bgfx particle buffer readback failed");
        for (std::size_t lane = 0U; lane < 20U; ++lane)
            Require(result[lane] == native_.Records()[0][lane], "Native buffer readback disagrees with independent compute/texture readback");
        return result;
    }
private:
    ParticleGpuReadback native_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uniform_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle output_ = BGFX_INVALID_HANDLE, readback_ = BGFX_INVALID_HANDLE;
};

struct MotionCase {
    kb::scene::ParticleEffectAsset effect;
    std::vector<kb::particles::ParticleGpuMotionModule> expected;
};

MotionCase MakeMotionCase(int kind) {
    MotionCase result;
    auto& effect = result.effect;
    effect.effectId = 7301U;
    effect.displayName = "Motion Readback";
    effect.recipeCategory = "Simple";
    effect.determinismSeed = 17U;
    effect.durationSeconds = 10.0F;
    effect.looping = true;
    effect.emitters.emplace_back();
    auto& emitter = effect.emitters.front();
    emitter.emitterId = 1U;
    emitter.name = "Motion";
    emitter.maxParticles = 8U;
    emitter.localPosition = {0.3F, 1.7F, -0.6F};
    emitter.spawn.rateOverTime.keyframes = {{.time = 0.0F, .value = 0.0F}};
    emitter.spawn.speedMin = emitter.spawn.speedMax = 2.0F;
    emitter.spawn.lifetimeMin = emitter.spawn.lifetimeMax = 8.0F;
    emitter.spawn.direction = {1.0F, 0.25F, -0.1F};
    emitter.spawn.randomization = emitter.spawn.spreadDegrees = 0.0F;
    emitter.output.material.virtualPath = "/Game/Particle.kbmat";
    emitter.output.blend = kb::scene::ParticleBlendMode::Add;
    const auto add = [&](std::uint64_t id, Module type, kb::scene::ParticleModulePayload payload,
                         kb::particles::ParticleGpuMotionModule gpu) {
        emitter.modules.push_back({.moduleId = id, .authoringOrder = static_cast<std::uint32_t>(emitter.modules.size()),
            .type = type, .payload = std::move(payload)});
        result.expected.push_back(gpu);
    };
    if (kind == 0 || kind >= 3) {
        add(0xFEDCBA98U, Module::Noise, kb::scene::ParticleNoiseModule{.strength = 0.7F, .frequency = 0.5F},
            {.type = Motion::Noise, .strength = 0.7F, .parameter = 0.5F, .seed = 0xFEDCBA98U});
        add(0x89ABCDEFU, Module::Noise, kb::scene::ParticleNoiseModule{.strength = 1.3F, .frequency = 2.0F},
            {.type = Motion::Noise, .strength = 1.3F, .parameter = 2.0F, .seed = 0x89ABCDEFU});
    }
    if (kind == 1 || kind >= 3) {
        add(3U, Module::AxisRotation, kb::scene::ParticleAxisRotationModule{.axis = {0.0F, 1.0F, 0.0F}, .degreesPerSecond = 90.0F},
            {.type = Motion::AxisRotation, .vector = {0.0F, 1.0F, 0.0F}, .strength = 90.0F * 0.017453292F});
        add(4U, Module::AxisRotation, kb::scene::ParticleAxisRotationModule{.axis = {1.0F, 0.0F, 0.0F}, .degreesPerSecond = 60.0F},
            {.type = Motion::AxisRotation, .vector = {1.0F, 0.0F, 0.0F}, .strength = 60.0F * 0.017453292F});
    }
    if (kind == 2 || kind >= 3) {
        add(5U, Module::Attraction, kb::scene::ParticleAttractionModule{.point = {1.0F, 2.0F, 0.0F}, .strength = 3.0F, .radius = 4.0F},
            {.type = Motion::Attraction, .vector = {1.0F, 2.0F, 0.0F}, .strength = 3.0F, .parameter = 4.0F});
        add(6U, Module::Attraction, kb::scene::ParticleAttractionModule{.point = {-2.0F, 0.0F, 3.0F}, .strength = 5.0F},
            {.type = Motion::Attraction, .vector = {-2.0F, 0.0F, 3.0F}, .strength = 5.0F});
    }
    if (kind >= 3) {
        add(7U, Module::Wind, kb::scene::ParticleWindModule{.acceleration = {0.2F, 0.3F, -0.1F}},
            {.type = Motion::Acceleration, .vector = {0.2F, 0.3F, -0.1F}});
        add(8U, Module::Drag, kb::scene::ParticleDragModule{.coefficient = 0.4F}, {.type = Motion::Drag, .strength = 0.4F});
        // Interleave the two fields of every type and the velocity-dependent drag.
        const std::array<std::size_t, 8> order{0U, 2U, 6U, 4U, 7U, 1U, 3U, 5U};
        const auto original = emitter.modules;
        const auto expected = result.expected;
        for (std::size_t i = 0U; i < order.size(); ++i) {
            emitter.modules[i] = original[order[i]];
            emitter.modules[i].authoringOrder = static_cast<std::uint32_t>(i);
            result.expected[i] = expected[order[i]];
        }
    }
    if (kind == 4) {
        while (emitter.modules.size() < kb::particles::kParticleGpuMaxMotionModules) {
            const auto id = static_cast<std::uint32_t>(emitter.modules.size() + 20U);
            if (id == 28U) {
                const kb::math::Vec3 acceleration{};
                add(id, Module::Gravity, kb::scene::ParticleGravityModule{.acceleration = acceleration, .sceneGravityScale = 0.5F},
                    {.type = Motion::Acceleration, .vector = acceleration + kb::scene::kParticleEffectDefaultSceneGravity * 0.5F});
            } else add(id, Module::Noise, kb::scene::ParticleNoiseModule{.strength = 0.15F, .frequency = 0.3F},
                {.type = Motion::Noise, .strength = 0.15F, .parameter = 0.3F, .seed = id});
        }
    }
    return result;
}

std::array<float, 20> RunAuthoredMotion(kb::scene::ParticleEffectAsset effect, bool gpu, MotionReadback& readback) {
    const auto root = std::filesystem::temp_directory_path() / ("21kb_particle_motion_" + std::to_string(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    effect.backendPolicy = gpu ? kb::scene::ParticleBackendPolicy::GpuVisualPreferred : kb::scene::ParticleBackendPolicy::CpuDeterministic;
    // Source storage is canonical by ID; execution order is a separate authored property.
    auto& stored = effect.emitters[0].modules;
    std::sort(stored.begin(), stored.end(), [](const auto& a, const auto& b) { return a.moduleId < b.moduleId; });
    const auto saved = kb::scene::ParticleEffectAssetIO::SaveDetailed(root / "Motion.kbvfx", effect);
    for (const auto& diagnostic : saved.diagnostics)
        std::fprintf(stderr, "motion_source %s: %s\n", diagnostic.propertyPath.c_str(), diagnostic.message.c_str());
    Require(saved.Succeeded(), "Motion source write failed");
    kb::project::ProjectDescriptor project;
    project.disableEnginePluginsByDefault = true;
    project.plugins.push_back({.name = "Rendering.21kbParticle", .binaryPath = KB_21KB_PARTICLE_PLUGIN_PATH, .enabled = true});
    std::array<float, 20> result{};
    {
        kb::scene::Scene scene(project, kb::scene::SceneMode::Runtime);
        Require(kb::particles::ParticlePlayback::HasBackend(scene), "Motion provider module unavailable");
        auto& manager = scene.Assets().Manager();
        static_cast<void>(manager.RegisterLoader(std::make_unique<kb::scene::ParticleEffectAssetLoader>()));
        Require(manager.Mounts().Mount("Game", root) && manager.DiscoverMountedAssets() >= 1U, "Motion source discovery failed");
        Require(manager.Registry().Upsert({.id = kb::assets::AssetId{72U}, .type = "RenderMaterial", .name = "Particle",
            .virtualPath = "/Game/Particle.kbmat", .contentHash = 1U}), "Motion material registry failed");
        const auto* asset = manager.Registry().FindByPath("/Game/Motion.kbvfx");
        Require(asset != nullptr, "Motion source asset missing");
        const auto owner = scene.Entities().CreateEntity();
        if (effect.emitters[0].simulationSpace == kb::scene::ParticleSimulationSpace::Local) {
            scene.Transforms().Set(owner, {.localPosition = {2.0F, -1.0F, 3.0F},
                .localRotation = kb::math::FromEulerDegrees({15.0F, 35.0F, -10.0F}), .localScale = {2.0F, 0.5F, 1.5F}});
        } else scene.Transforms().Set(owner, {});
        kb::particles::ParticlePlayback::SetGpuEmitterConsumer(scene, gpu);
        scene.Components().ParticleEffects().Set(owner, {.effectAssetId = asset->id.value, .enabled = true, .autoPlay = true});
        Require(scene.Runtime().Update(kStep), "Motion initial runtime step failed");
        const auto ids = kb::particles::ParticlePlayback::LiveInstanceIds(scene);
        Require(ids.size() == 1U && kb::particles::ParticlePlayback::Emit(scene, ids[0], 1U).Succeeded(), "Motion birth failed");
        ParticleGpuEmitterSimulation simulation;
        Require(!gpu || simulation.Initialize(), "Real GPU simulation unavailable");
        for (int step = 0; step < 60; ++step) {
            Require(scene.Runtime().Update(kStep), "Motion runtime step failed");
            if (gpu) {
                std::vector<kb::particles::ParticleGpuEmitterCommand> commands;
                kb::particles::ParticlePlayback::DrainGpuEmitterCommands(scene, commands);
                Require(!commands.empty(), "GPU burst emitter lost its simulation clock");
                simulation.Apply(1U, commands);
                simulation.Advance(1U, kStep);
                simulation.Dispatch(0U, 1U, {});
                static_cast<void>(bgfx::frame());
            }
        }
        const auto snapshot = kb::particles::ParticlePlayback::ReadRenderSnapshot(scene);
        Require(snapshot && snapshot->Emitters().size() == 1U, "Motion execution report missing");
        const auto& emitter = snapshot->Emitters()[0];
        if (gpu) {
            Require(emitter.simulationPath == kb::particles::ParticleEmitterSimulationPath::Gpu &&
                emitter.gpuFallbackReason == kb::particles::ParticleGpuFallbackReason::None && snapshot->Particles().empty(),
                "GPU motion test fell back to CPU");
            const auto draws = simulation.Draws(1U);
            Require(draws.size() == 1U, "GPU motion dispatch did not produce its instance buffer");
            const auto pixels = readback.Read(draws[0]);
            std::copy_n(pixels.begin(), result.size(), result.begin());
            Require(simulation.AllocatedBytes() == 8U * 144U, "GPU motion resource bytes changed unexpectedly");
            simulation.Shutdown();
        } else {
            Require(snapshot->Particles().size() == 1U, "CPU motion reference lost its particle");
            const auto& particle = snapshot->Particles()[0];
            result = {particle.position.x, particle.position.y, particle.position.z, particle.size,
                0.0F, 0.0F, 0.0F, 0.0F, particle.velocity.x, particle.velocity.y, particle.velocity.z};
        }
    }
    std::filesystem::remove_all(root, error);
    return result;
}

std::array<float, 20> RunMotionKernel(const MotionCase& test, MotionReadback& readback) {
    ParticleGpuEmitterSimulation simulation;
    Require(simulation.Initialize(), "Real GPU kernel unavailable");
    kb::particles::ParticleGpuEmitterCommand command;
    command.key = {1U, 1U};
    command.hasParams = true;
    command.params.capacity = 8U;
    command.params.blend = kb::particles::ParticleRenderBlendMode::Add;
    command.params.size.fill(1.0F);
    command.params.color.fill({1.0F, 1.0F, 1.0F, 1.0F});
    command.params.motionModuleCount = static_cast<std::uint32_t>(test.expected.size());
    std::copy(test.expected.begin(), test.expected.end(), command.params.motionModules.begin());
    command.hasField = true;
    command.fieldOrigin = test.effect.emitters[0].localPosition;
    command.spawns.push_back({.position = command.fieldOrigin,
        .velocity = kb::math::Normalize(test.effect.emitters[0].spawn.direction) * 2.0F, .lifetime = 8.0F});
    simulation.Apply(1U, {&command, 1U});
    simulation.Dispatch(0U, 1U, {});
    static_cast<void>(bgfx::frame());
    for (int step = 1; step <= 60; ++step) {
        kb::particles::ParticleGpuEmitterCommand clock;
        clock.key = command.key;
        clock.simTime = static_cast<double>(step) * kStep;
        simulation.Apply(1U, {&clock, 1U});
        simulation.Advance(1U, kStep);
        simulation.Dispatch(0U, 1U, {});
        static_cast<void>(bgfx::frame());
    }
    Require(simulation.Draws(1U).size() == 1U, "GPU kernel did not produce instances");
    const auto pixels = readback.Read(simulation.Draws(1U)[0]);
    std::array<float, 20> result;
    std::copy_n(pixels.begin(), result.size(), result.begin());
    simulation.Shutdown();
    return result;
}

void VerifyNativeReadbackBounds() {
    ParticleGpuEmitterSimulation simulation;
    Require(simulation.Initialize(), "Native readback GPU kernel unavailable");
    kb::particles::ParticleGpuEmitterCommand command;
    command.key = {11U, 22U};
    command.hasParams = true;
    command.params.capacity = 3U;
    command.params.blend = kb::particles::ParticleRenderBlendMode::Add;
    command.params.size.fill(1.0F);
    command.params.color.fill({1.0F, 1.0F, 1.0F, 1.0F});
    for (int index = 1; index <= 3; ++index)
        command.spawns.push_back({.position = {static_cast<float>(index), 0.0F, 0.0F}, .lifetime = 8.0F});
    const auto dispatch = [&] {
        simulation.Apply(1U, {&command, 1U});
        simulation.Dispatch(0U, 1U, {});
        static_cast<void>(bgfx::frame());
        Require(simulation.Draws(1U).size() == 1U, "Native readback lost its actual GPU draw");
    };
    dispatch();
    auto draw = simulation.Draws(1U)[0];
    Require(draw.key == command.key && draw.capacity == 3U && draw.requestedBufferBytes == 3U * 112U,
        "GPU draw identity or requested buffer accounting changed");
    ParticleGpuReadback capture;
    Require(capture.Read(draw, 3U) && capture.Records().size() == 3U, "Native readback could not read multiple rows");
    for (std::size_t index = 0; index < 3U; ++index)
        Require(capture.Records()[index][0] == static_cast<float>(index + 1U) && capture.Records()[index][3] == 1.0F,
            "Native readback changed GPU buffer offsets or record stride");
    Require(!capture.Read(draw, 0U) && !capture.Read(draw, 4U) && capture.Records().size() == 3U &&
        capture.Status() == ParticleGpuReadbackStatus::InvalidRequest, "Invalid readback count destroyed a completed capture");
    draw.capacity = kParticleGpuReadbackMaxInstances + 1U;
    Require(!capture.Read(draw, draw.capacity), "Native readback admitted an oversized capture");
    draw.instances = BGFX_INVALID_HANDLE;
    Require(!capture.Read(draw, 1U), "Native readback admitted an invalid GPU buffer");
    command.spawns = {{.position = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, .lifetime = 8.0F}};
    dispatch();
    Require(!capture.Read(simulation.Draws(1U)[0], 1U) && capture.Status() == ParticleGpuReadbackStatus::InvalidData &&
        capture.Records().size() == 3U && capture.Records()[0][0] == 1.0F,
        "Nonfinite GPU data replaced a completed capture or became a fabricated dead result");
    simulation.ReleaseScene(1U);
    Require(simulation.AllocatedBytes() == 0U && simulation.Draws(1U).empty(), "Released GPU inspection retained emitter buffers");
    simulation.Shutdown();
    std::fprintf(stderr, "particle_native_readback rows=3 bounds=checked invalid_data=rejected previous_capture=retained release_bytes=0\n");
}

void VerifyContactReadback(MotionReadback& readback) {
    ParticleGpuEmitterSimulation simulation;
    Require(simulation.Initialize(), "Contact GPU kernel unavailable");
    kb::particles::ParticleGpuEmitterCommand command;
    command.key = {1U, 1U};
    command.hasParams = true;
    command.params.capacity = 1U;
    command.params.hasPlane = true;
    command.params.plane.killOnContact = true;
    command.params.blend = kb::particles::ParticleRenderBlendMode::Add;
    command.params.size.fill(1.0F);
    command.params.color.fill({1.0F, 1.0F, 1.0F, 1.0F});
    command.spawns.push_back({.position = {0.0F, 0.2F, 0.0F}, .velocity = {0.0F, -12.0F, 0.0F}, .lifetime = 8.0F});
    const auto capture = [&] {
        simulation.Apply(1U, {&command, 1U});
        simulation.Dispatch(0U, 1U, {});
        static_cast<void>(bgfx::frame());
        Require(simulation.Draws(1U).size() == 1U, "Contact GPU dispatch missing");
        return readback.Read(simulation.Draws(1U)[0]);
    };
    const auto born = capture();
    Require(born[3] == 1.0F && born[1] == 0.2F && born[9] == -12.0F, "Contact GPU birth record changed");
    command.hasParams = false;
    command.spawns.clear();
    command.simTime = 0.2;
    const auto dead = capture();
    command.simTime = 0.4;
    const auto retained = capture();
    Require(std::all_of(dead.begin(), dead.begin() + 20U, [](float v) { return v == 0.0F; }) &&
        std::all_of(retained.begin(), retained.begin() + 20U, [](float v) { return v == 0.0F; }),
        "Contact GPU render records remained live or revived");
    command.spawns.push_back({.position = {0.0F, 1.0F, 0.0F}, .lifetime = 8.0F});
    const auto reused = capture();
    Require(reused[3] == 1.0F && reused[1] == 1.0F, "Contact GPU slot could not be reused with birth time zero");
    simulation.Shutdown();
    std::fprintf(stderr, "particle_contact_float_readback birth=live contact=dead retained=dead reused=live\n");
}
#endif
} // namespace

void RunParticleGpuMotionTests(bool kernelOnly) {
#if defined(_WIN32) && defined(KB_21KB_PARTICLE_PLUGIN_PATH)
    MotionSurface surface;
    DisplayConfig config;
    config.syncMode = DisplaySyncMode::Uncapped;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Direct3D11);
    Renderer renderer;
    Require(surface.NativeWindowHandle() != nullptr && renderer.Initialize(surface, &config), "Real bgfx D3D11 device unavailable");
    Require(bgfx::getRendererType() == bgfx::RendererType::Direct3D11, "Particle GPU motion used an unexpected backend");
    {
        MotionReadback readback;
        VerifyNativeReadbackBounds();
        VerifyContactReadback(readback);
        for (int kind = 0; kind < 5; ++kind) {
            std::array<float, 20> baseline{};
            for (int variant = 0; variant < (kind >= 3 ? 4 : 2); ++variant) {
                auto test = MakeMotionCase(kind);
                auto& modules = test.effect.emitters[0].modules;
                if (variant == 1) {
                    modules[0].enabled = false;
                    test.expected.erase(test.expected.begin());
                } else if (variant == 2) {
                    std::reverse(modules.begin(), modules.end());
                    std::reverse(test.expected.begin(), test.expected.end());
                    for (std::size_t index = 0; index < modules.size(); ++index)
                        modules[index].authoringOrder = static_cast<std::uint32_t>(index);
                } else if (variant == 3) {
                    for (auto& module : modules) {
                        if (module.type == Module::Noise) std::get<kb::scene::ParticleNoiseModule>(module.payload).strength = 0.0F;
                        else if (module.type == Module::AxisRotation) std::get<kb::scene::ParticleAxisRotationModule>(module.payload).degreesPerSecond = 0.0F;
                        else if (module.type == Module::Attraction) std::get<kb::scene::ParticleAttractionModule>(module.payload).strength = 0.0F;
                    }
                    for (auto& module : test.expected) if (module.type >= Motion::Noise) module.strength = 0.0F;
                } else if (kind < 4) {
                    modules.push_back({.moduleId = 99U, .authoringOrder = static_cast<std::uint32_t>(modules.size()),
                        .type = Module::Noise, .enabled = false,
                        .payload = kb::scene::ParticleNoiseModule{.strength = 100.0F, .frequency = 7.0F}});
                }
                const auto cpu = RunAuthoredMotion(test.effect, false, readback);
                const auto gpu = kernelOnly ? RunMotionKernel(test, readback) : RunAuthoredMotion(test.effect, true, readback);
                float positionError = 0.0F, velocityError = 0.0F, controlDifference = 0.0F;
                for (std::size_t axis = 0; axis < 3U; ++axis) {
                    Require(std::isfinite(gpu[axis]) && std::isfinite(gpu[8U + axis]), "GPU motion readback contained nonfinite values");
                    positionError = std::max(positionError, std::abs(cpu[axis] - gpu[axis]));
                    velocityError = std::max(velocityError, std::abs(cpu[8U + axis] - gpu[8U + axis]));
                    controlDifference = std::max({controlDifference, std::abs(cpu[axis] - baseline[axis]),
                        std::abs(cpu[8U + axis] - baseline[8U + axis])});
                }
                std::fprintf(stderr, "particle_motion kind=%d variant=%d kernel_only=%d modules=%zu position_error=%.9g velocity_error=%.9g control_difference=%.9g alive=%g\n",
                    kind, variant, kernelOnly ? 1 : 0, test.expected.size(), positionError, velocityError, controlDifference, gpu[3]);
                // Single-precision noise derivatives and transcendental functions accumulate backend rounding over 60 steps.
                Require(gpu[3] > 0.0F && positionError <= 0.001F && velocityError <= 0.003F, "GPU motion disagrees with CPU simulation");
                if (variant == 0) baseline = cpu;
                else Require(controlDifference > 0.01F, "Motion control did not expose a missing or reordered module");
            }
        }
        if (!kernelOnly) {
            for (int kind = 0; kind < 4; ++kind) {
                auto test = MakeMotionCase(kind);
                test.effect.emitters[0].simulationSpace = kb::scene::ParticleSimulationSpace::Local;
                const auto cpu = RunAuthoredMotion(test.effect, false, readback);
                const auto gpu = RunAuthoredMotion(test.effect, true, readback);
                float positionError = 0.0F, velocityError = 0.0F;
                for (std::size_t axis = 0; axis < 3U; ++axis) {
                    positionError = std::max(positionError, std::abs(cpu[axis] - gpu[axis]));
                    velocityError = std::max(velocityError, std::abs(cpu[8U + axis] - gpu[8U + axis]));
                }
                std::fprintf(stderr, "particle_motion_local kind=%d position_error=%.9g velocity_error=%.9g\n", kind, positionError, velocityError);
                Require(gpu[3] > 0.0F && positionError <= 0.001F && velocityError <= 0.003F,
                    "Local-space GPU motion changed authored world-axis forces under owner rotation/scale");
            }
        }
    }
    renderer.Shutdown();
#else
    static_cast<void>(kernelOnly);
    Require(false, "Particle GPU motion verification requires the Windows GPU host and particle provider");
#endif
}
} // namespace kb::render::tests
