#include "ParticleRuntimeDiagnostics.hpp"

#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/ShaderLoader.hpp"
#include "kb/render/particles/ParticleGpuReadback.hpp"

#include <algorithm>
#include <string>

namespace kb::game {
namespace {
using kb::core::JsonValue;
JsonValue Integer(std::uint64_t value) { return JsonValue::MakeString(std::to_string(value)); }

JsonValue CaptureImage(kb::scene::Scene& scene, kb::render::Renderer& renderer) {
    using namespace kb::render;
    auto result = JsonValue::MakeObject();
    result.Set("succeeded", JsonValue::MakeBool(false));
    result.Set("scope", JsonValue::MakeString("terminal 64x64 offscreen scene without post processing or overlays; no simulation update"));
    SceneRenderTarget target;
    if (!target.Ensure(64U, 64U, SceneColorFormatPolicy::Rgba8)) return result;
    const RenderSceneSubmitDesc desc{
        .target = {.frameBuffer = target.FrameBuffer(), .colorTexture = target.ColorTexture(),
            .depthTexture = target.DepthTexture(), .viewport = {.id = {2U}, .extent = {64U, 64U}},
            .colorFormat = target.ColorSelection().format, .depthFormat = target.DepthSelection().format},
        .clearRgba = 0x000000FFU, .editorSceneOverlaysEnabled = false, .screenUIEnabled = false,
        .shadowPassEnabled = false, .postProcessEnabled = false, .selectionMaskEnabled = false, .selectionOutlineEnabled = false};
    const auto before = scene.Runtime().FixedStepIndex();
    const auto id = kb::scene::SceneRenderFeedback::RequestScreenCapturePixels(scene);
    renderer.SetFrameDeltaSeconds(0.0F);
    if (id == 0U || !renderer.BeginFrame()) return result;
    const bool submitted = renderer.SubmitScene(scene, desc);
    const bool renderClean = !renderer.LastSceneDiagnostics().HasErrors() && !renderer.LastSceneSubmitStats().HasMissingResources();
    renderer.EndFrame();
    for (std::uint32_t drain = 0U; drain < 16U &&
            kb::scene::SceneRenderFeedback::ScreenCaptureStatus(scene, id) == kb::scene::SceneScreenCaptureStatus::Pending; ++drain) {
        if (!renderer.BeginFrame()) break;
        renderer.EndFrame();
    }
    const auto pixels = kb::scene::SceneRenderFeedback::TakeScreenCapturePixels(scene, id);
    if (!submitted || !renderClean || !pixels || pixels->width != 64U || pixels->height != 64U || pixels->bytes.size() != 64U * 64U * 4U ||
        (pixels->format != kb::scene::SceneScreenCapturePixelFormat::Rgba8 && pixels->format != kb::scene::SceneScreenCapturePixelFormat::Bgra8) ||
        scene.Runtime().FixedStepIndex() != before) return result;
    const bool bgra = pixels->format == kb::scene::SceneScreenCapturePixelFormat::Bgra8;
    constexpr std::string_view digits = "0123456789abcdef";
    std::string rgba;
    rgba.reserve(pixels->bytes.size() * 2U);
    for (std::size_t offset = 0U; offset < pixels->bytes.size(); offset += 4U) {
        for (const auto channel : {bgra ? 2U : 0U, 1U, bgra ? 0U : 2U, 3U}) {
            const auto value = pixels->bytes[offset + channel];
            rgba.push_back(digits[value >> 4U]);
            rgba.push_back(digits[value & 15U]);
        }
    }
    result.Set("succeeded", JsonValue::MakeBool(true));
    result.Set("width", JsonValue::MakeNumber(64U));
    result.Set("height", JsonValue::MakeNumber(64U));
    result.Set("rgba8_hex", JsonValue::MakeString(std::move(rgba)));
    return result;
}
}

ParticleRuntimeDiagnosticResult CaptureParticleRuntimeDiagnostics(
    kb::scene::Scene& scene, kb::render::Renderer& renderer,
    std::uint32_t samples, bool packaged, std::uint32_t renderedFrames,
    std::uint32_t observedCompute, std::uint32_t observedParticleDraws) {
    ParticleRuntimeDiagnosticResult result;
    auto& report = result.report;
    report = JsonValue::MakeObject();
    report.Set("schema", JsonValue::MakeString("21kb.particle-runtime/v1"));
    report.Set("packaged", JsonValue::MakeBool(packaged));
    report.Set("exclusive_package_shaders", JsonValue::MakeBool(kb::render::ShaderLoader::HasBinaryProvider()));
    report.Set("backend", JsonValue::MakeString(bgfx::getRendererName(bgfx::getRendererType())));
    report.Set("rendered_frames", JsonValue::MakeNumber(renderedFrames));
    report.Set("fixed_step", Integer(scene.Runtime().FixedStepIndex()));
    report.Set("fixed_delta_seconds", JsonValue::MakeNumber(1.0F / 60.0F));
    report.Set("observed_max_compute_submissions", JsonValue::MakeNumber(observedCompute));
    report.Set("observed_max_particle_draws", JsonValue::MakeNumber(observedParticleDraws));
    report.Set("requested_gpu_buffer_bytes", Integer(renderer.GpuEmitterAllocatedBytes()));
    report.Set("total_gpu_sample_limit", JsonValue::MakeNumber(kb::render::kParticleGpuReadbackMaxInstances));
    const auto snapshot = kb::particles::ParticlePlayback::ReadRenderSnapshot(scene);
    const auto draws = renderer.GpuParticleDraws(scene);
    bool allGpu = snapshot && !snapshot->Emitters().empty();
    auto emitters = JsonValue::MakeArray();
    std::uint32_t remainingCpuSamples = kb::render::kParticleGpuReadbackMaxInstances;
    if (snapshot) {
        for (const auto& emitter : snapshot->Emitters()) {
            auto row = JsonValue::MakeObject();
            const bool gpu = emitter.simulationPath == kb::particles::ParticleEmitterSimulationPath::Gpu;
            const bool matchingDraw = std::any_of(draws.begin(), draws.end(), [&](const auto& draw) {
                return draw.key.instanceId == emitter.instanceId && draw.key.emitterId == emitter.emitterId;
            });
            allGpu = allGpu && gpu && matchingDraw && emitter.particleCount == 0U &&
                emitter.gpuFallbackReason == kb::particles::ParticleGpuFallbackReason::None;
            row.Set("instance_id", Integer(emitter.instanceId));
            row.Set("emitter_id", Integer(emitter.emitterId));
            row.Set("effect_asset_id", Integer(emitter.effectAssetId));
            row.Set("path", JsonValue::MakeString(gpu ? "gpu" : "cpu"));
            row.Set("reason", JsonValue::MakeString(std::string{kb::particles::ParticleGpuFallbackReasonName(emitter.gpuFallbackReason)}));
            row.Set("cpu_render_record_count", JsonValue::MakeNumber(emitter.particleCount));
            auto records = JsonValue::MakeArray();
            const auto particles = snapshot->Particles();
            if (emitter.firstParticle <= particles.size() && emitter.particleCount <= particles.size() - emitter.firstParticle) {
                const auto count = std::min({samples, emitter.particleCount, remainingCpuSamples});
                remainingCpuSamples -= count;
                for (const auto& particle : particles.subspan(emitter.firstParticle, count)) {
                    auto record = JsonValue::MakeArray();
                    for (const auto value : {particle.position.x, particle.position.y, particle.position.z,
                            particle.velocity.x, particle.velocity.y, particle.velocity.z}) record.Append(JsonValue::MakeNumber(value));
                    records.Append(std::move(record));
                }
            }
            row.Set("cpu_position_velocity_samples", std::move(records));
            emitters.Append(std::move(row));
        }
    }
    report.Set("emitters", std::move(emitters));
    bool captured = samples > 0U && samples <= kb::render::kParticleGpuReadbackMaxInstances &&
        draws.size() <= kb::render::kParticleGpuReadbackMaxInstances;
    const auto perEmitterSamples = draws.empty() ? samples : std::min(samples,
        kb::render::kParticleGpuReadbackMaxInstances / static_cast<std::uint32_t>(draws.size()));
    report.Set("gpu_samples_per_emitter", JsonValue::MakeNumber(perEmitterSamples));
    auto captures = JsonValue::MakeArray();
    if (captured) for (const auto& draw : draws) {
        auto row = JsonValue::MakeObject();
        row.Set("instance_id", Integer(draw.key.instanceId));
        row.Set("emitter_id", Integer(draw.key.emitterId));
        row.Set("capacity", JsonValue::MakeNumber(draw.capacity));
        row.Set("output_enum", JsonValue::MakeNumber(static_cast<std::uint8_t>(draw.params->output)));
        row.Set("plane_kill_on_contact", JsonValue::MakeBool(draw.params->hasPlane && draw.params->plane.killOnContact));
        auto motion = JsonValue::MakeArray();
        const auto moduleCount = std::min(draw.params->motionModuleCount, static_cast<std::uint32_t>(draw.params->motionModules.size()));
        captured = captured && moduleCount == draw.params->motionModuleCount;
        for (std::uint32_t index = 0U; index < moduleCount; ++index) {
            const auto& operation = draw.params->motionModules[index];
            auto op = JsonValue::MakeObject();
            op.Set("type", JsonValue::MakeNumber(static_cast<std::uint8_t>(operation.type)));
            op.Set("strength", JsonValue::MakeNumber(operation.strength));
            op.Set("parameter", JsonValue::MakeNumber(operation.parameter));
            op.Set("seed", Integer(operation.seed));
            auto vector = JsonValue::MakeArray();
            for (const auto value : {operation.vector.x, operation.vector.y, operation.vector.z}) vector.Append(JsonValue::MakeNumber(value));
            op.Set("vector", std::move(vector));
            motion.Append(std::move(op));
        }
        row.Set("motion_modules", std::move(motion));
        kb::render::ParticleGpuReadback capture;
        const bool read = capture.Read(draw, std::min(perEmitterSamples, draw.capacity));
        captured = captured && read;
        row.Set("readback_succeeded", JsonValue::MakeBool(read));
        auto records = JsonValue::MakeArray();
        if (read) for (const auto& record : capture.Records()) {
            auto values = JsonValue::MakeArray();
            for (const auto value : record) values.Append(JsonValue::MakeNumber(value));
            records.Append(std::move(values));
        }
        row.Set("raw_instance_records", std::move(records));
        captures.Append(std::move(row));
    }
    report.Set("gpu_captures", std::move(captures));
    result.gpuExecutionObserved = allGpu && !draws.empty() && captured && observedCompute > 0U && observedParticleDraws > 0U;
    result.succeeded = snapshot && !snapshot->Emitters().empty() && captured &&
        (!packaged || kb::render::ShaderLoader::HasBinaryProvider());
    auto image = CaptureImage(scene, renderer);
    const auto* imageSucceeded = image.Find("succeeded");
    result.succeeded = result.succeeded && imageSucceeded != nullptr && imageSucceeded->AsBool();
    report.Set("image", std::move(image));
    report.Set("gpu_execution_observed", JsonValue::MakeBool(result.gpuExecutionObserved));
    report.Set("succeeded", JsonValue::MakeBool(result.succeeded));
    return result;
}

} // namespace kb::game
