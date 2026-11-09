// Included by ParticleEditorTests.cpp to exercise the editor's actual provider.
void TestGpuEventLifecycle() {
#if defined(_WIN32)
    using Playback = kb::particles::ParticlePlayback;
    using Trigger = kb::scene::ParticleEventTrigger;
    constexpr float step = kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds;
    class Surface final : public kb::render::RenderSurface {
    public:
        HWND window = CreateWindowExW(0U, L"STATIC", L"Particle events", WS_OVERLAPPEDWINDOW,
            0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~Surface() override { if (window) DestroyWindow(window); }
        std::uint32_t Width() const noexcept override { return 64U; }
        std::uint32_t Height() const noexcept override { return 64U; }
        void* NativeWindowHandle() const noexcept override { return window; }
        void* NativeDisplayHandle() const noexcept override { return nullptr; }
    } surface;
    kb::render::DisplayConfig config;
    config.syncMode = kb::render::DisplaySyncMode::Uncapped;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Direct3D11);
    kb::render::Renderer renderer;
    Require(surface.window && renderer.Initialize(surface, &config) &&
        bgfx::getRendererType() == bgfx::RendererType::Direct3D11, "GPU events require the real D3D11 renderer");
    kb::project::ProjectDescriptor project;
    project.disableEnginePluginsByDefault = true;
    project.plugins.push_back({.name = "Rendering.21kbParticle",
        .binaryPath = KB_21KB_PARTICLE_PLUGIN_PATH, .enabled = true});
    kb::assets::AssetRegistry registry;
    Require(registry.Upsert({.id = kb::assets::AssetId{72U}, .type = "RenderMaterial",
        .virtualPath = "/Game/Materials/PreviewParticle.21kb", .contentHash = 1U}), "GPU event material registration failed");
    for (const std::string_view mode : {"birth", "death", "restart", "collision", "overflow"}) {
        const bool collision = mode == "collision", overflow = mode == "overflow", restart = mode == "restart";
        auto asset = MakeEffect(0.0F);
        asset.backendPolicy = kb::scene::ParticleBackendPolicy::GpuVisualPreferred;
        auto& parent = asset.emitters[0];
        const auto parentCount = overflow ? kb::scene::kParticleEffectMaxEventsPerStep + 1U : 1U;
        parent.maxParticles = overflow ? parentCount : 8U;
        parent.spawn.lifetimeMin = parent.spawn.lifetimeMax = 4.0F * step;
        parent.spawn.direction = collision ? kb::math::Vec3{0.0F, -1.0F, 0.0F} : kb::math::Vec3{1.0F, 0.0F, 0.0F};
        parent.spawn.speedMin = parent.spawn.speedMax = 6.0F;
        parent.spawn.randomization = parent.spawn.spreadDegrees = 0.0F;
        parent.localPosition = {0.0F, 0.02F, 0.0F};
        parent.output.blend = kb::scene::ParticleBlendMode::Add;
        auto child = parent;
        child.emitterId = 22U;
        child.authoringOrder = 1U;
        child.name = "Child";
        child.maxParticles = 8U;
        child.localPosition = {};
        child.spawn.lifetimeMin = child.spawn.lifetimeMax = 1.0F;
        child.spawn.direction = {1.0F, 0.0F, 0.0F};
        child.spawn.speedMin = child.spawn.speedMax = 1.0F;
        if (collision) parent.modules.push_back({.moduleId = 1U, .type = kb::scene::ParticleModuleType::CollisionPlane,
            .payload = kb::scene::ParticleCollisionPlaneModule{.killOnContact = true}});
        asset.emitters.push_back(std::move(child));
        asset.eventBindings.push_back({.sourceEmitterId = 11U,
            .trigger = collision ? Trigger::Collision : (mode == "birth" ? Trigger::Birth : Trigger::Death),
            .action = kb::scene::ParticleEventAction::EmitTargetEmitter, .targetEmitterId = 22U,
            .count = 2U, .maxDepth = 1U, .perStepBudget = 1U});
        kb::particle_editor::ParticlePreviewSession preview;
        Require(preview.Start(project, registry, kb::assets::AssetId{71U},
            "/Game/Effects/GpuEvents.kbvfx", asset).Succeeded(), "GPU event preview failed to start");
        auto& scene = preview.PreviewScene();
        Playback::SetGpuEmitterConsumer(scene, true);
        kb::render::ParticleGpuEmitterSimulation simulation;
        kb::render::ParticleGpuReadback capture;
        Require(simulation.Initialize(), "GPU event compute programs unavailable");
        std::vector<kb::particles::ParticleGpuSpawn> parentBirths, childBirths;
        std::uint32_t rejected = 0U, clearCommands = 0U;
        double lastTime = 0.0, renderTime = 0.0;
        const auto tick = [&] {
            Require(preview.Tick(step).Succeeded(), "GPU event editor preview tick failed");
            std::vector<kb::particles::ParticleGpuEmitterCommand> commands;
            Playback::DrainGpuEmitterCommands(scene, commands);
            for (const auto& command : commands) {
                auto& births = command.key.emitterId == 11U ? parentBirths : childBirths;
                births.insert(births.end(), command.spawns.begin(), command.spawns.end());
                if (command.clear) ++clearCommands;
                lastTime = std::max(lastTime, command.simTime);
            }
            simulation.Apply(1U, commands);
            renderTime = std::max(renderTime, lastTime - 1.0 / 60.0);
            renderTime = std::clamp(renderTime + step, lastTime - 1.0 / 60.0, lastTime + 1.0 / 60.0);
            simulation.Advance(1U, step);
            simulation.Dispatch(0U, 1U, {});
            static_cast<void>(bgfx::frame());
            const auto snapshot = Playback::ReadRenderSnapshot(scene);
            Require(snapshot && snapshot->Emitters().size() == 2U, "GPU event execution snapshot missing emitters");
            for (const auto& emitter : snapshot->Emitters()) {
                rejected += emitter.rejectedByEventBudget;
                if (emitter.emitterId != 11U) continue;
                Require(collision ? emitter.simulationPath == kb::particles::ParticleEmitterSimulationPath::Cpu &&
                    emitter.gpuFallbackReason == kb::particles::ParticleGpuFallbackReason::CollisionEvent :
                    emitter.simulationPath == kb::particles::ParticleEmitterSimulationPath::Gpu &&
                    emitter.gpuFallbackReason == kb::particles::ParticleGpuFallbackReason::None,
                    "GPU event route/fallback diagnostic disagrees with the authored trigger");
            }
        };
        tick();
        const auto ids = Playback::LiveInstanceIds(scene);
        Require(ids.size() == 1U && Playback::Emit(scene, ids[0], parentCount).Succeeded(),
            "GPU event parent emission failed");
        tick();
        if (restart) {
            Require(Playback::Restart(scene, ids[0]).Succeeded(), "GPU event restart failed");
            for (int i = 0; i < 8; ++i) tick();
            Require(childBirths.empty() && clearCommands >= 2U, "restart retained scheduled GPU death events or missed a clear command");
            for (const auto& draw : simulation.Draws(1U)) {
                Require(capture.Read(draw, draw.capacity), "GPU event restart readback failed");
                Require(std::all_of(capture.Records().begin(), capture.Records().end(),
                    [](const auto& record) { return record[3] == 0.0F; }), "restart retained live compute instances");
            }
            Require(Playback::Emit(scene, ids[0], 1U).Succeeded(), "GPU event emission after restart failed");
        }
        for (int i = 0; i < 10; ++i) tick();
        Require(childBirths.size() == 2U, "GPU event budget changed the admitted child count");
        if (overflow) Require(rejected >= kb::scene::kParticleEffectMaxEventsPerStep,
            "GPU event queue/binding overflow was not reported in editor telemetry");
        if (!collision) {
            const auto& source = parentBirths.back();
            const auto expected = source.position + source.velocity * (mode == "birth" ? 0.0F : source.lifetime);
            for (const auto& birth : childBirths) Require(std::abs(birth.position.x - expected.x) < 0.001F &&
                std::abs(birth.position.y - expected.y) < 0.001F && std::abs(birth.position.z - expected.z) < 0.001F,
                "GPU sub-emitter child did not start at the birth/death event position");
        } else Require(std::abs(childBirths[0].position.y) < 0.001F, "collision fallback child missed the world plane");
        const auto draws = simulation.Draws(1U);
        const auto childDraw = std::find_if(draws.begin(), draws.end(), [](const auto& draw) { return draw.key.emitterId == 22U; });
        Require(childDraw != draws.end() && capture.Read(*childDraw, childDraw->capacity), "GPU sub-emitter child readback unavailable");
        std::size_t alive = 0U;
        for (const auto& record : capture.Records()) {
            if (record[3] == 0.0F) continue;
            ++alive;
            const auto& birth = childBirths[0];
            const auto expected = birth.position + birth.velocity * static_cast<float>(renderTime - birth.birthTime);
            if (std::abs(record[0] - expected.x) >= 0.001F || std::abs(record[1] - expected.y) >= 0.001F)
                std::cerr << "gpu-event trajectory mode=" << mode << " actual=" << record[0] << ',' << record[1]
                    << " expected=" << expected.x << ',' << expected.y << " clock=" << renderTime
                    << " latest=" << lastTime << " birth=" << birth.birthTime << " age=" << record[17] << '\n';
            Require(std::abs(record[0] - expected.x) < 0.001F && std::abs(record[1] - expected.y) < 0.001F &&
                std::abs(record[2] - expected.z) < 0.001F && std::abs(record[8] - birth.velocity.x) < 0.001F &&
                std::abs(record[17] - static_cast<float>((renderTime - birth.birthTime) / birth.lifetime)) < 0.001F,
                "GPU child compute readback disagrees with its event trajectory");
        }
        Require(alive == 2U, "GPU compute did not simulate exactly the admitted children");
        std::cout << "gpu-events " << mode << " children=" << alive << " rejected=" << rejected << " clears=" << clearCommands << '\n';
        capture.Shutdown();
        preview.Release([&](const kb::scene::Scene&) { simulation.ReleaseScene(1U); });
        Require(simulation.AllocatedBytes() == 0U, "GPU sub-emitter buffers survived editor preview release");
        simulation.Shutdown();
    }
    renderer.Shutdown();
#else
    Require(false, "GPU event readback requires the Windows D3D11 host");
#endif
}
