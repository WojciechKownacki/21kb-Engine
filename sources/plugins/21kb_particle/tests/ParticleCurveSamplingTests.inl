// Included by ParticleEditorTests.cpp to share its real editor/provider fixture.
void TestCurveSamplingAccuracy() {
    using Playback = kb::particles::ParticlePlayback;
    using namespace kb::particle_editor;
    constexpr float epsilon = 0.00002F;
    static_assert(kb::particles::kParticleGpuCurveSamples == 8U);
    const auto root = TestRoot() / ("curve-sampling-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    kb::project::ProjectDescriptor project;
    project.disableEnginePluginsByDefault = true;
    project.plugins.push_back({.name = "Rendering.21kbParticle",
        .binaryPath = KB_21KB_PARTICLE_PLUGIN_PATH, .enabled = true});
    kb::assets::AssetRegistry registry;
    Require(registry.Upsert({.id = kb::assets::AssetId{72U}, .type = "RenderMaterial",
        .virtualPath = "/Game/Materials/PreviewParticle.21kb", .contentHash = 1U}),
        "curve material registration failed");
    for (const std::string_view fixture : {"linear", "quadratic", "narrow", "sharp"}) {
        kb::math::Curve alpha;
        if (fixture == "narrow") {
            alpha.keyframes = {{.time = 0.0F, .value = 0.0F}, {.time = 0.499F, .value = 0.0F},
                {.time = 0.5F, .value = 1.0F}, {.time = 0.501F, .value = 0.0F}, {.time = 1.0F, .value = 0.0F}};
        } else if (fixture == "sharp") {
            alpha.keyframes = {{.time = 0.0F, .value = 0.0F}, {.time = 0.499F, .value = 0.0F},
                {.time = 0.501F, .value = 1.0F}, {.time = 1.0F, .value = 1.0F}};
        } else {
            alpha.keyframes = {{.time = 0.0F, .value = 0.0F,
                .easing = fixture == "quadratic" ? kb::math::Easing::InQuad : kb::math::Easing::Linear},
                {.time = 1.0F, .value = 1.0F}};
        }
        auto size = alpha;
        for (auto& key : size.keyframes) key.value += 1.0F;
        kb::math::Gradient gradient;
        for (const auto& key : alpha.keyframes)
            gradient.stops.push_back({.time = key.time, .color = {key.value, 1.0F - key.value, 0.25F, 1.0F}});
        auto asset = MakeEffect(0.0F);
        asset.backendPolicy = kb::scene::ParticleBackendPolicy::GpuVisualPreferred;
        asset.emitters[0].spawn.speedMin = asset.emitters[0].spawn.speedMax = 0.0F;
        asset.emitters[0].modules = {
            {.moduleId = 1U, .authoringOrder = 0U, .type = kb::scene::ParticleModuleType::SizeOverLife,
                .payload = kb::scene::ParticleSizeOverLifeModule{.curve = size}},
            {.moduleId = 2U, .authoringOrder = 1U, .type = kb::scene::ParticleModuleType::ColorOverLife,
                .payload = kb::scene::ParticleColorOverLifeModule{.gradient = gradient}},
            {.moduleId = 3U, .authoringOrder = 2U, .type = kb::scene::ParticleModuleType::AlphaOverLife,
                .payload = kb::scene::ParticleAlphaOverLifeModule{.curve = alpha}}};
        const auto source = root / (std::string{fixture} + ".kbvfx");
        ParticleAssetGateway gateway;
        Require(gateway.Save(source, asset).Succeeded(), "curve asset save failed");
        const auto reloaded = gateway.Load(source);
        Require(reloaded.asset.has_value(), "curve asset reload failed");
        std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;
        Require(kb::scene::ParticleEffectAssetIO::Serialize(asset, diagnostics) ==
            kb::scene::ParticleEffectAssetIO::Serialize(*reloaded.asset, diagnostics),
            "save/reload lost a curve key, easing or gradient stop");
        const kb::assets::AssetMetadata owner{.id = kb::assets::AssetId{71U},
            .type = kb::scene::kParticleEffectAssetType, .virtualPath = "/Game/Effects/Curves.kbvfx",
            .physicalPath = source, .contentHash = 1U};
        const auto baked = ParticleBakeService::Bake({.workingAsset = *reloaded.asset,
            .owner = owner, .registry = registry, .cacheRoot = root / std::string{fixture}});
        Require(baked.Succeeded(), "curve production bake failed");
        const auto cached = kb::particles::ParticleCompiledEffectCache::Load(baked.cachePath, baked.key);
        Require(cached.Succeeded(), "curve cooked cache reload failed");
        const auto& compiled = cached.effect->emitters[0];
        Require(compiled.sizeOverLife.keyCount == size.keyframes.size() &&
            compiled.alphaOverLife.keyCount == alpha.keyframes.size() &&
            compiled.colorOverLife.stopCount == gradient.stops.size(), "cook lost curve or gradient entries");
        for (std::size_t i = 0U; i < alpha.keyframes.size(); ++i) {
            Require(compiled.sizeOverLife.keys[i].time == size.keyframes[i].time &&
                compiled.sizeOverLife.keys[i].value == size.keyframes[i].value &&
                compiled.sizeOverLife.keys[i].easing == size.keyframes[i].easing &&
                compiled.alphaOverLife.keys[i].time == alpha.keyframes[i].time &&
                compiled.alphaOverLife.keys[i].value == alpha.keyframes[i].value &&
                compiled.alphaOverLife.keys[i].easing == alpha.keyframes[i].easing &&
                compiled.colorOverLife.stops[i].time == gradient.stops[i].time &&
                compiled.colorOverLife.stops[i].color.r == gradient.stops[i].color.r &&
                compiled.colorOverLife.stops[i].color.g == gradient.stops[i].color.g &&
                compiled.colorOverLife.stops[i].color.b == gradient.stops[i].color.b &&
                compiled.colorOverLife.stops[i].color.a == gradient.stops[i].color.a,
                "cook changed curve keys, easing or gradient values");
        }
        const auto exact = [&](float age) {
            const auto color = kb::math::Evaluate(gradient, age);
            return std::array<float, 5U>{kb::math::Evaluate(size, age), color.r, color.g, color.b,
                color.a * kb::math::Evaluate(alpha, age)};
        };
        ParticlePreviewSession preview;
        Require(preview.Start(project, registry, kb::assets::AssetId{71U},
            "/Game/Effects/Curves.kbvfx", *reloaded.asset).Succeeded(), "curve editor preview failed");
        auto& scene = preview.PreviewScene();
        Playback::SetGpuEmitterConsumer(scene, true);
        Require(preview.Tick(kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(), "curve preview tick failed");
        const auto gpuInstances = Playback::LiveInstanceIds(scene);
        Require(gpuInstances.size() == 1U && Playback::Emit(scene, gpuInstances[0], 1U).Succeeded() &&
            preview.Tick(kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(), "GPU curve birth failed");
        std::vector<kb::particles::ParticleGpuEmitterCommand> commands;
        Playback::DrainGpuEmitterCommands(scene, commands);
        const auto command = std::find_if(commands.begin(), commands.end(), [](const auto& value) { return value.hasParams; });
        Require(command != commands.end(), "preview did not publish GPU appearance samples");
        for (std::size_t i = 0U; i < kb::particles::kParticleGpuCurveSamples; ++i) {
            const auto expected = exact(static_cast<float>(i) / 7.0F);
            Require(std::abs(command->params.size[i] - expected[0]) < epsilon, "GPU size sample differs from authored curve");
            for (std::size_t channel = 0U; channel < 4U; ++channel)
                Require(std::abs(command->params.color[i][channel] - expected[channel + 1U]) < epsilon,
                    "GPU color/alpha sample differs from authored gradient/curve");
        }
        // Alternative resolutions quantify approximation only; the GPU ABI remains eight samples.
        for (const std::size_t count : {4U, 8U, 16U, 32U}) {
            std::vector<std::array<float, 5U>> table;
            for (std::size_t i = 0U; i < count; ++i) table.push_back(exact(static_cast<float>(i) / (count - 1U)));
            if (count == kb::particles::kParticleGpuCurveSamples) {
                for (std::size_t i = 0U; i < count; ++i) table[i] = {command->params.size[i],
                    command->params.color[i][0], command->params.color[i][1], command->params.color[i][2], command->params.color[i][3]};
            }
            const auto sample = [&](float age) {
                const float scaled = std::clamp(age, 0.0F, 1.0F) * static_cast<float>(count - 1U);
                const auto index = std::min(static_cast<std::size_t>(scaled), count - 2U);
                auto result = table[index];
                for (std::size_t channel = 0U; channel < result.size(); ++channel)
                    result[channel] = kb::math::Lerp(table[index][channel], table[index + 1U][channel], scaled - index);
                return result;
            };
            std::vector<float> critical{0.0F, 1.0F};
            for (const auto& key : alpha.keyframes) {
                critical.push_back(key.time);
                critical.push_back(std::nextafter(key.time, 0.0F));
                critical.push_back(std::nextafter(key.time, 1.0F));
            }
            for (std::size_t i = 0U; i < count; ++i) critical.push_back(static_cast<float>(i) / (count - 1U));
            const auto errorAt = [&](float age) {
                const auto reference = exact(age), approximation = sample(age);
                float error = 0.0F;
                for (std::size_t channel = 0U; channel < reference.size(); ++channel)
                    error = std::max(error, std::abs(reference[channel] - approximation[channel]));
                return error;
            };
            float bound = 0.0F;
            for (const auto age : critical) bound = std::max(bound, errorAt(age));
            // The difference of two piecewise linear functions reaches its maximum at their knots.
            // For t^2, the exact interpolation error on a cell of width h is h^2/4.
            if (fixture == "quadratic") bound = 1.0F / (4.0F * (count - 1U) * (count - 1U));
            float measured = 0.0F;
            for (std::size_t i = 0U; i <= 32768U; ++i)
                measured = std::max(measured, errorAt(static_cast<float>(i) / 32768.0F));
            for (const auto age : critical) measured = std::max(measured, errorAt(age));
            Require(measured <= bound + epsilon, "curve LUT exceeded its analytical interpolation bound");
            if (fixture == "linear") Require(measured < epsilon, "linear curve/gradient did not interpolate exactly");
            if (fixture == "narrow") Require(measured > 0.999F, "narrow peak fixture no longer exposes undersampling");
            if (fixture == "sharp") Require(measured > 0.46F && measured < 0.501F, "sharp transition error changed unexpectedly");
            Require(errorAt(0.0F) < epsilon && errorAt(1.0F) < epsilon &&
                sample(-0.1F) == sample(0.0F) && sample(1.1F) == sample(1.0F), "LUT lifetime endpoints or clamping changed");
            std::cout << "curve-sampling " << fixture << " samples=" << count << " max-error=" << measured << " bound=" << bound << '\n';
        }
        preview.Release([](const kb::scene::Scene&) {});
        // Exercise the same reloaded authoring data through CPU simulation and editor snapshots.
        auto cpuAsset = *reloaded.asset;
        cpuAsset.backendPolicy = kb::scene::ParticleBackendPolicy::CpuDeterministic;
        ParticlePreviewSession cpu;
        Require(cpu.Start(project, registry, kb::assets::AssetId{71U}, "/Game/Effects/Curves.kbvfx", cpuAsset).Succeeded() &&
            cpu.Tick(kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(), "CPU curve preview failed");
        const auto instances = Playback::LiveInstanceIds(cpu.PreviewScene());
        Require(instances.size() == 1U && Playback::Emit(cpu.PreviewScene(), instances[0], 1U).Succeeded(), "CPU curve birth failed");
        std::size_t observed = 0U;
        for (std::size_t frame = 0U; frame < 121U; ++frame) {
            Require(cpu.Tick(kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(), "CPU curve simulation failed");
            const auto states = Playback::LiveParticleStates(cpu.PreviewScene(), instances[0]);
            if (states.empty()) continue;
            ++observed;
            const auto& state = states[0];
            const auto expected = exact(state.age / state.lifetime);
            Require(std::abs(state.size - expected[0]) < epsilon && std::abs(state.color.r - expected[1]) < epsilon &&
                std::abs(state.color.g - expected[2]) < epsilon && std::abs(state.color.b - expected[3]) < epsilon &&
                std::abs(state.color.a - expected[4]) < epsilon, "CPU simulation disagreed with authored curve/gradient");
        }
        Require(observed >= 119U, "CPU curve test did not span the particle lifetime");
        const auto snapshot = Playback::ReadRenderSnapshot(cpu.PreviewScene());
        Require(snapshot && snapshot->Particles().empty(), "expired curve particle remained in editor snapshot");
        cpu.Release([](const kb::scene::Scene&) {});
    }
}
