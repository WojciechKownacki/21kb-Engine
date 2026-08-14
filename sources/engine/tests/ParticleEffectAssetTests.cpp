#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/ParticleEffectAssetLoader.hpp"
#include "engine/scene/ParticleEffectAssetMigration.hpp"
#include "engine/scene/ParticleEffectAssetValidation.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void Require(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error{std::string{message}};
}

[[nodiscard]] std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    Require(input.is_open(), "test fixture could not be opened");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void WriteText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    Require(output.is_open(), "test file could not be opened for writing");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    Require(static_cast<bool>(output), "test file write failed");
}

[[nodiscard]] bool HasDiagnostic(const std::vector<kb::scene::ParticleEffectDiagnostic>& diagnostics,
                                 kb::scene::ParticleEffectDiagnosticCode code, std::string_view path = {}) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [&](const kb::scene::ParticleEffectDiagnostic& diagnostic) {
                           return diagnostic.code == code && (path.empty() || diagnostic.propertyPath == path);
                       });
}

void ReplaceOnce(std::string& source, std::string_view from, std::string_view to) {
    const std::size_t position = source.find(from);
    Require(position != std::string::npos, "test mutation source was not found");
    source.replace(position, from.size(), to);
}

void ReplaceLine(std::string& source, std::string_view key, std::string_view replacement) {
    const std::size_t begin = source.find(key);
    Require(begin != std::string::npos, "test line mutation source was not found");
    const std::size_t end = source.find('\n', begin);
    Require(end != std::string::npos, "test line mutation source has no newline");
    source.replace(begin, end - begin, replacement);
}

[[nodiscard]] kb::math::Curve UnitCurve(float value = 1.0F) {
    return kb::math::Curve{
        .keyframes = {kb::math::CurveKeyframe{.time = 0.0F, .value = value, .easing = kb::math::Easing::Linear}}};
}

[[nodiscard]] kb::math::Gradient UnitGradient() {
    return kb::math::Gradient{
        .stops = {
            kb::math::GradientStop{.time = 0.0F, .color = kb::math::Color{1.0F, 0.5F, 0.25F, 1.0F}},
            kb::math::GradientStop{.time = 1.0F, .color = kb::math::Color{0.0F, 0.0F, 0.0F, 0.0F}},
        }};
}

[[nodiscard]] kb::scene::ParticleEffectAsset MakeComprehensiveAsset() {
    using namespace kb::scene;
    ParticleEffectAsset asset{};
    asset.effectId = 9001U;
    asset.displayName = "Comprehensive effect";
    asset.recipeCategory = "Tests";
    asset.determinismSeed = std::numeric_limits<std::uint64_t>::max();
    asset.durationSeconds = 4.5F;
    asset.backendPolicy = ParticleBackendPolicy::GpuVisualPreferred;
    asset.gpuCatchupPolicy = ParticleGpuCatchupPolicy::BoundedWarmup;

    for (std::size_t index = 0U; index < 8U; ++index) {
        ParticleEmitterAsset emitter{};
        emitter.emitterId = 10U + index;
        emitter.name = "Emitter " + std::to_string(index);
        emitter.simulationSpace = index % 2U == 0U ? ParticleSimulationSpace::Local : ParticleSimulationSpace::World;
        emitter.spawn.mode = index % 2U == 0U ? ParticleSpawnMode::Continuous : ParticleSpawnMode::Burst;
        emitter.spawn.rateOverTime = UnitCurve(2.0F + static_cast<float>(index));
        if (emitter.spawn.mode == ParticleSpawnMode::Burst)
            emitter.spawn.bursts.push_back(ParticleBurstAsset{.timeSeconds = 0.25F, .count = 3U});
        emitter.output.type = static_cast<ParticleOutputType>(index);
        emitter.output.payload = DefaultParticleOutputPayload(emitter.output.type);
        emitter.output.material.assetId = 100U + index;
        if (index == 0U)
            emitter.output.material.virtualPath = "/Game/Materials/Primary.kbmat";
        emitter.output.blend = static_cast<ParticleBlendMode>(index % 6U);
        emitter.output.sort = static_cast<ParticleSortMode>(index % 5U);
        emitter.output.alignment = static_cast<ParticleAlignment>(index % 4U);
        if (index < 3U)
            emitter.output.textureAtlas.assetId = 200U + index;
        if (emitter.output.type == ParticleOutputType::Mesh)
            emitter.output.mesh.assetId = 300U;
        if (auto* value = std::get_if<ParticleBillboardOutput>(&emitter.output.payload))
            value->flipbook =
                ParticleFlipbookAsset{.columns = 2U, .rows = 2U, .framesPerSecond = 12.0F, .looping = false};
        if (auto* value = std::get_if<ParticleStretchedBillboardOutput>(&emitter.output.payload))
            value->flipbook =
                ParticleFlipbookAsset{.columns = 2U, .rows = 1U, .framesPerSecond = 8.0F, .looping = true};
        if (auto* value = std::get_if<ParticlePointSpriteOutput>(&emitter.output.payload))
            value->flipbook =
                ParticleFlipbookAsset{.columns = 1U, .rows = 2U, .framesPerSecond = 6.0F, .looping = true};
        asset.emitters.push_back(std::move(emitter));
    }

    ParticleEmitterAsset& first = asset.emitters.front();
    first.modules = {
        ParticleModuleAsset{
            .moduleId = 1U, .type = ParticleModuleType::InitialVelocity, .payload = ParticleInitialVelocityModule{}},
        ParticleModuleAsset{.moduleId = 2U, .type = ParticleModuleType::Gravity, .payload = ParticleGravityModule{}},
        ParticleModuleAsset{.moduleId = 3U, .type = ParticleModuleType::Wind, .payload = ParticleWindModule{}},
        ParticleModuleAsset{
            .moduleId = 4U, .type = ParticleModuleType::Drag, .payload = ParticleDragModule{.coefficient = 0.25F}},
        ParticleModuleAsset{.moduleId = 5U,
                            .type = ParticleModuleType::ColorOverLife,
                            .payload = ParticleColorOverLifeModule{.gradient = UnitGradient()}},
        ParticleModuleAsset{.moduleId = 6U,
                            .type = ParticleModuleType::SizeOverLife,
                            .payload = ParticleSizeOverLifeModule{.curve = UnitCurve()}},
        ParticleModuleAsset{.moduleId = 7U,
                            .type = ParticleModuleType::AlphaOverLife,
                            .payload = ParticleAlphaOverLifeModule{.curve = UnitCurve()}},
        ParticleModuleAsset{
            .moduleId = 8U, .type = ParticleModuleType::CollisionPlane, .payload = ParticleCollisionPlaneModule{}},
        ParticleModuleAsset{.moduleId = 9U,
                            .type = ParticleModuleType::SubEmitter,
                            .payload = ParticleSubEmitterModule{.targetEmitterId = 17U,
                                                                .trigger = ParticleEventTrigger::Death,
                                                                .count = 2U,
                                                                .maxDepth = 2U}},
    };
    asset.eventBindings = {
        ParticleEventBindingAsset{.sourceEmitterId = 10U,
                                  .trigger = ParticleEventTrigger::Birth,
                                  .sourceModuleId = 1U,
                                  .action = ParticleEventAction::EmitTargetEmitter,
                                  .targetEmitterId = 11U,
                                  .count = 1U,
                                  .maxDepth = 2U,
                                  .perStepBudget = 32U},
        ParticleEventBindingAsset{.sourceEmitterId = 10U,
                                  .trigger = ParticleEventTrigger::Death,
                                  .sourceModuleId = 2U,
                                  .action = ParticleEventAction::EmitEffectAsset,
                                  .targetEffect = ParticleAssetReference{.assetId = 700U},
                                  .count = 2U,
                                  .maxDepth = 2U,
                                  .perStepBudget = 32U},
        ParticleEventBindingAsset{.sourceEmitterId = 10U,
                                  .trigger = ParticleEventTrigger::Collision,
                                  .sourceModuleId = 8U,
                                  .action = ParticleEventAction::EmitTargetEmitter,
                                  .targetEmitterId = 12U,
                                  .count = 1U,
                                  .maxDepth = 1U,
                                  .perStepBudget = 16U},
    };
    return asset;
}

void RunCanonicalGoldenTest() {
    const std::filesystem::path fixture =
        std::filesystem::path{KB_PARTICLE_ASSET_TEST_SOURCE_DIR} / "fixtures" / "CanonicalParticleEffectV2.kbvfx";
    const std::string authoredCanonical = ReadText(fixture);
    const kb::scene::ParticleEffectLoadResult parsed = kb::scene::ParticleEffectAssetIO::Parse(authoredCanonical);
    Require(parsed.Succeeded(), "hand-authored canonical v2 fixture did not parse");
    Require(parsed.asset->displayName == "Żar \"A\"\\B\n", "quoted Unicode or escapes did not decode exactly");
    std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;
    const std::optional<std::string> serialized =
        kb::scene::ParticleEffectAssetIO::Serialize(*parsed.asset, diagnostics);
    Require(serialized.has_value() && *serialized == authoredCanonical,
            "hand-authored canonical fixture is not byte stable");
    Require(serialized->find("0.100000001") != std::string::npos, "canonical float precision changed");
    Require(serialized->find('\r') == std::string::npos, "writer emitted a non-canonical newline");
}

void RunComprehensiveSchemaTest() {
    using namespace kb::scene;
    const ParticleEffectAsset asset = MakeComprehensiveAsset();
    Require(ParticleEffectAssetValidator::ValidateStructure(asset).Succeeded(),
            "comprehensive v2 asset failed structural validation");
    std::vector<ParticleEffectDiagnostic> diagnostics;
    const std::optional<std::string> encoded = ParticleEffectAssetIO::Serialize(asset, diagnostics);
    Require(encoded.has_value(), "comprehensive v2 asset did not serialize");
    const ParticleEffectLoadResult decoded = ParticleEffectAssetIO::Parse(*encoded);
    Require(decoded.Succeeded() && decoded.asset->emitters.size() == 8U && decoded.asset->eventBindings.size() == 3U,
            "comprehensive v2 asset did not round-trip");
    for (std::size_t index = 0U; index < 8U; ++index) {
        Require(decoded.asset->emitters[index].output.type == static_cast<ParticleOutputType>(index),
                "typed output variant changed during round-trip");
        Require(decoded.asset->emitters[index].output.payload.index() == index,
                "typed output payload changed during round-trip");
    }
    Require(decoded.asset->emitters.front().modules.size() == 9U, "typed module records did not round-trip");

    for (ParticleBackendPolicy policy :
         {ParticleBackendPolicy::CpuDeterministic, ParticleBackendPolicy::GpuVisualPreferred,
          ParticleBackendPolicy::GpuVisualRequired}) {
        ParticleEffectAsset variant = asset;
        variant.backendPolicy = policy;
        diagnostics.clear();
        Require(ParticleEffectAssetIO::Serialize(variant, diagnostics).has_value(),
                "backend policy enum is not serializable");
    }
    for (ParticleGpuCatchupPolicy policy :
         {ParticleGpuCatchupPolicy::RestartFromSeed, ParticleGpuCatchupPolicy::BoundedWarmup}) {
        ParticleEffectAsset variant = asset;
        variant.gpuCatchupPolicy = policy;
        diagnostics.clear();
        Require(ParticleEffectAssetIO::Serialize(variant, diagnostics).has_value(),
                "GPU catch-up policy enum is not serializable");
    }
    for (std::uint32_t value = 0U; value <= static_cast<std::uint32_t>(kb::math::Easing::InOutBounce); ++value) {
        ParticleEffectAsset variant = asset;
        variant.emitters[0].spawn.rateOverTime.keyframes[0].easing = static_cast<kb::math::Easing>(value);
        diagnostics.clear();
        const std::optional<std::string> text = ParticleEffectAssetIO::Serialize(variant, diagnostics);
        Require(text.has_value() && ParticleEffectAssetIO::Parse(*text).Succeeded(),
                "curve easing enum did not round-trip");
    }
}

void RunHardLimitBoundaryTest() {
    using namespace kb::scene;
    ParticleEffectAsset asset = MakeComprehensiveAsset();
    asset.displayName.assign(kParticleEffectMaxStringBytes, 'A');
    ParticleEmitterAsset& first = asset.emitters.front();
    first.maxParticles = kParticleEffectMaxCpuParticlesPerEmitter;
    for (ParticleStableId id = 10U; id <= 16U; ++id)
        first.modules.push_back(
            ParticleModuleAsset{.moduleId = id, .type = ParticleModuleType::Wind, .payload = ParticleWindModule{}});
    first.spawn.bursts.clear();
    first.spawn.rateOverTime.keyframes.clear();
    auto& gradient = std::get<ParticleColorOverLifeModule>(first.modules[4].payload).gradient;
    gradient.stops.clear();
    for (std::uint32_t index = 0U; index < kParticleEffectMaxCurveKeys; ++index) {
        const float time = static_cast<float>(index) / static_cast<float>(kParticleEffectMaxCurveKeys - 1U);
        first.spawn.rateOverTime.keyframes.push_back(
            kb::math::CurveKeyframe{.time = time, .value = 1.0F, .easing = kb::math::Easing::Linear});
        gradient.stops.push_back(
            kb::math::GradientStop{.time = time, .color = kb::math::Color{time, time, time, time}});
    }
    for (std::uint32_t index = 0U; index < kParticleEffectMaxBursts; ++index)
        first.spawn.bursts.push_back(
            ParticleBurstAsset{.timeSeconds = static_cast<float>(index), .count = kParticleEffectMaxSpawnsPerStep});
    std::get<ParticleCollisionPlaneModule>(first.modules[7].payload).maxEventsPerStep = kParticleEffectMaxEventsPerStep;
    std::get<ParticleTrailOutput>(asset.emitters[4].output.payload).maxSamplesPerParticle =
        kParticleEffectMaxTrailSamplesPerParticle;
    std::get<ParticleRibbonOutput>(asset.emitters[5].output.payload).maxSegments =
        kParticleEffectMaxStripSegmentsPerEmitter;
    std::get<ParticleBeamOutput>(asset.emitters[6].output.payload).segments = kParticleEffectMaxStripSegmentsPerEmitter;
    while (asset.eventBindings.size() < kParticleEffectMaxEventBindings) {
        asset.eventBindings.push_back(ParticleEventBindingAsset{.sourceEmitterId = 10U,
                                                                .trigger = ParticleEventTrigger::Death,
                                                                .action = ParticleEventAction::EmitEffectAsset,
                                                                .targetEffect = ParticleAssetReference{.assetId = 800U},
                                                                .count = kParticleEffectMaxSpawnsPerStep,
                                                                .maxDepth = kParticleEffectMaxSubEmitterDepth,
                                                                .perStepBudget = kParticleEffectMaxEventsPerStep});
    }
    Require(ParticleEffectAssetValidator::ValidateStructure(asset).Succeeded(),
            "schema hard-limit boundary asset was rejected");
    std::vector<ParticleEffectDiagnostic> diagnostics;
    const std::optional<std::string> encoded = ParticleEffectAssetIO::Serialize(asset, diagnostics);
    Require(encoded.has_value() && ParticleEffectAssetIO::Parse(*encoded).Succeeded(),
            "schema hard-limit boundary asset did not round-trip");

    ParticleEffectAsset recordBoundary = MakeComprehensiveAsset();
    for (ParticleEmitterAsset& emitter : recordBoundary.emitters) {
        emitter.modules.clear();
        kb::math::Curve fullCurve;
        kb::math::Gradient fullGradient;
        for (std::uint32_t index = 0U; index < kParticleEffectMaxCurveKeys; ++index) {
            const float time = static_cast<float>(index) / static_cast<float>(kParticleEffectMaxCurveKeys - 1U);
            fullCurve.keyframes.push_back(
                kb::math::CurveKeyframe{.time = time, .value = time, .easing = kb::math::Easing::Linear});
            fullGradient.stops.push_back(
                kb::math::GradientStop{.time = time, .color = kb::math::Color{time, time, time, time}});
        }
        emitter.modules = {
            ParticleModuleAsset{.moduleId = 1U,
                                .type = ParticleModuleType::ColorOverLife,
                                .payload = ParticleColorOverLifeModule{.gradient = fullGradient}},
            ParticleModuleAsset{.moduleId = 2U,
                                .type = ParticleModuleType::SizeOverLife,
                                .payload = ParticleSizeOverLifeModule{.curve = fullCurve}},
            ParticleModuleAsset{.moduleId = 3U,
                                .type = ParticleModuleType::AlphaOverLife,
                                .payload = ParticleAlphaOverLifeModule{.curve = fullCurve}},
        };
    }
    recordBoundary.eventBindings.clear();
    diagnostics.clear();
    const std::optional<std::string> recordText = ParticleEffectAssetIO::Serialize(recordBoundary, diagnostics);
    Require(recordText.has_value() &&
                static_cast<std::size_t>(std::count(recordText->begin(), recordText->end(), '\n')) > 4'096U &&
                ParticleEffectAssetIO::Parse(*recordText).Succeeded(),
            "legal high-record-count asset did not pass the bounded parser");
}

void RunBoundedFileLoadTest() {
    using namespace kb::scene;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_particle_asset_load_test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    Require(!error, "bounded file-load test directory could not be prepared");
    const std::filesystem::path oversized = root / "Oversized.kbvfx";
    WriteText(oversized, std::string(kParticleEffectMaxSourceBytes + 1U, 'x'));
    Require(HasDiagnostic(ParticleEffectAssetIO::LoadDetailed(oversized).diagnostics,
                          ParticleEffectDiagnosticCode::SourceTooLarge),
            "physical oversized source was not rejected before parsing");
    const std::filesystem::path empty = root / "Empty.kbvfx";
    WriteText(empty, {});
    Require(HasDiagnostic(ParticleEffectAssetIO::LoadDetailed(empty).diagnostics,
                          ParticleEffectDiagnosticCode::EmptySource),
            "physical empty source did not return EmptySource");
    Require(HasDiagnostic(ParticleEffectAssetIO::LoadDetailed(root / "Missing.kbvfx").diagnostics,
                          ParticleEffectDiagnosticCode::FileNotFound),
            "missing physical source did not return FileNotFound");
    Require(HasDiagnostic(ParticleEffectAssetIO::LoadDetailed(root).diagnostics,
                          ParticleEffectDiagnosticCode::FileAccessFailed),
            "non-regular source did not return FileAccessFailed");
}

void RunLegacyMigrationTest() {
    using namespace kb::scene;
    const std::filesystem::path fixture = std::filesystem::path{KB_PARTICLE_ASSET_TEST_SOURCE_DIR} / ".." / ".." /
                                          "editor" / "tests" / "fixtures" / "LegacyAutomationParticleEffect.kbvfx";
    const std::string before = ReadText(fixture);
    const auto timestamp = std::filesystem::last_write_time(fixture);
    const ParticleEffectLoadResult loaded = ParticleEffectAssetIO::LoadDetailed(fixture);
    Require(loaded.Succeeded() && loaded.migratedFromLegacy, "legacy fixture did not migrate in memory");
    Require(ReadText(fixture) == before && std::filesystem::last_write_time(fixture) == timestamp,
            "legacy load mutated its source file");
    Require(loaded.asset->formatVersion == 2U && loaded.asset->emitters.size() == 1U,
            "legacy effect envelope mapping is incomplete");
    const ParticleEmitterAsset& emitter = loaded.asset->emitters.front();
    Require(loaded.asset->looping && loaded.asset->durationSeconds == 2.0F && emitter.maxParticles == 128U,
            "legacy playback or capacity mapping is incomplete");
    Require(emitter.spawn.rateOverTime.keyframes.front().value == 16.0F && emitter.spawn.lifetimeMin == 0.5F &&
                emitter.spawn.lifetimeMax == 1.0F,
            "legacy spawn mapping is incomplete");
    Require(emitter.spawn.speedMin == 0.5F && emitter.spawn.speedMax == 1.5F && emitter.spawn.spreadDegrees == 20.0F,
            "legacy velocity mapping is incomplete");
    Require(ParticleEffectMaterialReference(*loaded.asset) == "/Game/Vfx/Particle.kbmat",
            "legacy material mapping is incomplete");
    const ParticleEffectLegacyView view = BuildParticleEffectLegacyView(*loaded.asset);
    Require(view.gravityScale == 0.1F && view.sizeOverLifetime != nullptr &&
                view.sizeOverLifetime->keyframes.size() == 2U && view.colorOverLifetime != nullptr &&
                view.colorOverLifetime->stops.size() == 2U,
            "legacy module mapping is incomplete");
}

void RunMalformedInputTest() {
    using namespace kb::scene;
    const std::filesystem::path fixture =
        std::filesystem::path{KB_PARTICLE_ASSET_TEST_SOURCE_DIR} / "fixtures" / "CanonicalParticleEffectV2.kbvfx";
    const std::string canonical = ReadText(fixture);
    const auto check = [&](std::string source, ParticleEffectDiagnosticCode code, std::string_view path,
                           bool requireLine) {
        const ParticleEffectLoadResult result = ParticleEffectAssetIO::Parse(source);
        Require(!result.Succeeded() && HasDiagnostic(result.diagnostics, code, path),
                "malformed input returned the wrong diagnostic");
        if (requireLine) {
            const auto found = std::find_if(result.diagnostics.begin(), result.diagnostics.end(),
                                            [&](const ParticleEffectDiagnostic& diagnostic) {
                                                return diagnostic.code == code && diagnostic.propertyPath == path;
                                            });
            Require(found != result.diagnostics.end() && found->line != 0U,
                    "malformed input diagnostic lost its source line");
        }
    };
    check(canonical + "effect.id 43\n", ParticleEffectDiagnosticCode::DuplicateKey, "effect.id", true);
    std::string missing = canonical;
    ReplaceOnce(missing, "effect.id 42\n", "");
    check(std::move(missing), ParticleEffectDiagnosticCode::MissingKey, "effect.id", true);
    check(canonical + "effect.unknown 1\n", ParticleEffectDiagnosticCode::UnknownKey, "effect.unknown", true);
    std::string future = canonical;
    ReplaceOnce(future, "21kb ParticleEffect 2", "21kb ParticleEffect 3");
    check(std::move(future), ParticleEffectDiagnosticCode::UnsupportedVersion, "effect.formatVersion", true);
    std::string badHeader = canonical;
    ReplaceOnce(badHeader, "21kb ParticleEffect 2", "21kb ParticleEffect two");
    check(std::move(badHeader), ParticleEffectDiagnosticCode::InvalidHeader, "effect.formatVersion", true);
    std::string nan = canonical;
    ReplaceOnce(nan, "effect.durationSeconds 2.5", "effect.durationSeconds NaN");
    check(std::move(nan), ParticleEffectDiagnosticCode::InvalidValue, "effect.durationSeconds", true);
    std::string infinity = canonical;
    ReplaceOnce(infinity, "effect.durationSeconds 2.5", "effect.durationSeconds Inf");
    check(std::move(infinity), ParticleEffectDiagnosticCode::InvalidValue, "effect.durationSeconds", true);
    std::string badEnum = canonical;
    ReplaceOnce(badEnum, "effect.backendPolicy CpuDeterministic", "effect.backendPolicy Unknown");
    check(std::move(badEnum), ParticleEffectDiagnosticCode::InvalidEnum, "effect.backendPolicy", true);
    std::string badCount = canonical;
    ReplaceOnce(badCount, "effect.emitterCount 1", "effect.emitterCount 9");
    check(std::move(badCount), ParticleEffectDiagnosticCode::LimitExceeded, "effect.emitterCount", true);
    std::string countMismatch = canonical;
    ReplaceOnce(countMismatch, "effect.emitterCount 1", "effect.emitterCount 2");
    check(std::move(countMismatch), ParticleEffectDiagnosticCode::CountMismatch, "effect.emitter[1].id", true);
    std::string badModuleCount = canonical;
    ReplaceOnce(badModuleCount, "effect.emitter[0].moduleCount 0", "effect.emitter[0].moduleCount 17");
    check(std::move(badModuleCount), ParticleEffectDiagnosticCode::LimitExceeded, "effect.emitter[0].moduleCount",
          true);
    std::string badBurstCount = canonical;
    ReplaceOnce(badBurstCount, "effect.emitter[0].spawn.burstCount 0", "effect.emitter[0].spawn.burstCount 65");
    check(std::move(badBurstCount), ParticleEffectDiagnosticCode::LimitExceeded, "effect.emitter[0].spawn.burstCount",
          true);
    std::string badCurveCount = canonical;
    ReplaceOnce(badCurveCount, "effect.emitter[0].spawn.rate.keyCount 1", "effect.emitter[0].spawn.rate.keyCount 65");
    check(std::move(badCurveCount), ParticleEffectDiagnosticCode::LimitExceeded,
          "effect.emitter[0].spawn.rate.keyCount", true);
    std::string badEventCount = canonical;
    ReplaceOnce(badEventCount, "effect.eventBindingCount 0", "effect.eventBindingCount 33");
    check(std::move(badEventCount), ParticleEffectDiagnosticCode::LimitExceeded, "effect.eventBindingCount", true);
    std::string invalidRange = canonical;
    ReplaceOnce(invalidRange, "effect.emitter[0].maxParticles 64", "effect.emitter[0].maxParticles 0");
    check(std::move(invalidRange), ParticleEffectDiagnosticCode::LimitExceeded, "effect.emitter[0].maxParticles", true);
    std::string badEscape = canonical;
    ReplaceOnce(badEscape, "effect.displayName \"Żar ", "effect.displayName \"\\q");
    check(std::move(badEscape), ParticleEffectDiagnosticCode::InvalidEscape, "effect.displayName", true);
    std::string escapedDelimiter = canonical;
    ReplaceLine(escapedDelimiter, "effect.displayName ", "effect.displayName \"abc\\\"");
    check(std::move(escapedDelimiter), ParticleEffectDiagnosticCode::InvalidEscape, "effect.displayName", true);
    std::string rawTab = canonical;
    ReplaceLine(rawTab, "effect.displayName ", "effect.displayName \"abc\tdef\"");
    check(std::move(rawTab), ParticleEffectDiagnosticCode::InvalidEscape, "effect.displayName", true);
    std::string invalidUtf8 = canonical;
    invalidUtf8[invalidUtf8.find("Emitter")] = static_cast<char>(0xFF);
    check(std::move(invalidUtf8), ParticleEffectDiagnosticCode::InvalidUtf8, "", false);
    std::string oversized(kParticleEffectMaxSourceBytes + 1U, 'x');
    check(std::move(oversized), ParticleEffectDiagnosticCode::SourceTooLarge, "", false);

    std::vector<ParticleEffectDiagnostic> diagnostics;
    std::string comprehensive = *ParticleEffectAssetIO::Serialize(MakeComprehensiveAsset(), diagnostics);
    std::string badGradientCount = comprehensive;
    ReplaceOnce(badGradientCount, "effect.emitter[0].module[4].payload.gradient.stopCount 2",
                "effect.emitter[0].module[4].payload.gradient.stopCount 65");
    check(std::move(badGradientCount), ParticleEffectDiagnosticCode::LimitExceeded,
          "effect.emitter[0].module[4].payload.gradient.stopCount", true);
    std::string duplicateId = comprehensive;
    ReplaceOnce(duplicateId, "effect.emitter[1].id 11", "effect.emitter[1].id 10");
    check(std::move(duplicateId), ParticleEffectDiagnosticCode::InvalidStableId, "effect.emitter[1].id", true);
    std::string unsortedId = comprehensive;
    ReplaceOnce(unsortedId, "effect.emitter[1].id 11", "effect.emitter[1].id 9");
    check(std::move(unsortedId), ParticleEffectDiagnosticCode::UnsortedStableId, "effect.emitter[1].id", true);

    const auto checkContext = [&](std::string source, std::string_view path, ParticleStableId emitterId,
                                  ParticleStableId moduleId) {
        const ParticleEffectLoadResult parsed = ParticleEffectAssetIO::Parse(source);
        const auto found = std::find_if(
            parsed.diagnostics.begin(), parsed.diagnostics.end(), [&](const ParticleEffectDiagnostic& diagnostic) {
                return diagnostic.code == ParticleEffectDiagnosticCode::InvalidValue && diagnostic.propertyPath == path;
            });
        Require(found != parsed.diagnostics.end() && found->line != 0U && found->emitterId == emitterId &&
                    found->moduleId == moduleId,
                "parser diagnostic lost stable record context");
    };
    std::string malformedModule = comprehensive;
    ReplaceOnce(malformedModule, "effect.emitter[0].module[3].payload.coefficient 0.25",
                "effect.emitter[0].module[3].payload.coefficient NaN");
    checkContext(std::move(malformedModule), "effect.emitter[0].module[3].payload.coefficient", 10U, 4U);
    std::string malformedOutput = comprehensive;
    ReplaceOnce(malformedOutput, "effect.emitter[4].output.payload.width 1",
                "effect.emitter[4].output.payload.width NaN");
    checkContext(std::move(malformedOutput), "effect.emitter[4].output.payload.width", 14U, 0U);
    std::string malformedEvent = comprehensive;
    ReplaceOnce(malformedEvent, "effect.eventBinding[0].count 1", "effect.eventBinding[0].count NaN");
    checkContext(std::move(malformedEvent), "effect.eventBinding[0].count", 10U, 1U);
}

void RunValidatorMatrixTest() {
    using namespace kb::scene;
    const ParticleEffectAsset valid = MakeComprehensiveAsset();
    const auto rejects = [&](std::string_view label, auto mutate, ParticleEffectDiagnosticCode code) {
        ParticleEffectAsset candidate = valid;
        mutate(candidate);
        Require(HasDiagnostic(ParticleEffectAssetValidator::ValidateStructure(candidate).diagnostics, code),
                std::string{"validator matrix case was accepted: "} + std::string{label});
    };
    rejects(
        "emitter ID", [](ParticleEffectAsset& value) { value.emitters[1].emitterId = value.emitters[0].emitterId; },
        ParticleEffectDiagnosticCode::InvalidStableId);
    rejects(
        "module ID",
        [](ParticleEffectAsset& value) {
            value.emitters[0].modules[1].moduleId = value.emitters[0].modules[0].moduleId;
        },
        ParticleEffectDiagnosticCode::InvalidStableId);
    rejects(
        "module duplicate",
        [](ParticleEffectAsset& value) {
            value.emitters[0].modules.push_back(value.emitters[0].modules[1]);
            value.emitters[0].modules.back().moduleId = 10U;
        },
        ParticleEffectDiagnosticCode::DuplicateModule);
    rejects(
        "curve order",
        [](ParticleEffectAsset& value) {
            std::get<ParticleSizeOverLifeModule>(value.emitters[0].modules[5].payload)
                .curve.keyframes.push_back({.time = 0.0F, .value = 1.0F});
        },
        ParticleEffectDiagnosticCode::InvalidCurve);
    rejects(
        "gradient range",
        [](ParticleEffectAsset& value) {
            std::get<ParticleColorOverLifeModule>(value.emitters[0].modules[4].payload).gradient.stops[0].color.r =
                2.0F;
        },
        ParticleEffectDiagnosticCode::InvalidGradient);
    rejects(
        "mesh reference", [](ParticleEffectAsset& value) { value.emitters[3].output.mesh = {}; },
        ParticleEffectDiagnosticCode::InvalidReference);
    rejects(
        "mesh wrong slot", [](ParticleEffectAsset& value) { value.emitters[0].output.mesh.assetId = 301U; },
        ParticleEffectDiagnosticCode::InvalidReference);
    rejects(
        "atlas wrong slot", [](ParticleEffectAsset& value) { value.emitters[4].output.textureAtlas.assetId = 302U; },
        ParticleEffectDiagnosticCode::InvalidReference);
    rejects(
        "module self-cycle",
        [](ParticleEffectAsset& value) {
            std::get<ParticleSubEmitterModule>(value.emitters[0].modules[8].payload).targetEmitterId = 10U;
        },
        ParticleEffectDiagnosticCode::CyclicReference);
    rejects(
        "event self-cycle", [](ParticleEffectAsset& value) { value.eventBindings[0].targetEmitterId = 10U; },
        ParticleEffectDiagnosticCode::CyclicReference);
    rejects(
        "three-emitter cycle with local depth one",
        [](ParticleEffectAsset& value) {
            value.eventBindings = {
                ParticleEventBindingAsset{.sourceEmitterId = 10U,
                                          .trigger = ParticleEventTrigger::Birth,
                                          .action = ParticleEventAction::EmitTargetEmitter,
                                          .targetEmitterId = 11U,
                                          .count = 1U,
                                          .maxDepth = 1U,
                                          .perStepBudget = 1U},
                ParticleEventBindingAsset{.sourceEmitterId = 11U,
                                          .trigger = ParticleEventTrigger::Birth,
                                          .action = ParticleEventAction::EmitTargetEmitter,
                                          .targetEmitterId = 12U,
                                          .count = 1U,
                                          .maxDepth = 1U,
                                          .perStepBudget = 1U},
                ParticleEventBindingAsset{.sourceEmitterId = 12U,
                                          .trigger = ParticleEventTrigger::Birth,
                                          .action = ParticleEventAction::EmitTargetEmitter,
                                          .targetEmitterId = 10U,
                                          .count = 1U,
                                          .maxDepth = 1U,
                                          .perStepBudget = 1U},
            };
        },
        ParticleEffectDiagnosticCode::CyclicReference);
    ParticleEffectAsset locallyBoundedDepth = valid;
    locallyBoundedDepth.eventBindings = {
        ParticleEventBindingAsset{.sourceEmitterId = 10U,
                                  .trigger = ParticleEventTrigger::Birth,
                                  .action = ParticleEventAction::EmitTargetEmitter,
                                  .targetEmitterId = 11U,
                                  .count = 1U,
                                  .maxDepth = 3U,
                                  .perStepBudget = 1U},
        ParticleEventBindingAsset{.sourceEmitterId = 11U,
                                  .trigger = ParticleEventTrigger::Birth,
                                  .action = ParticleEventAction::EmitTargetEmitter,
                                  .targetEmitterId = 12U,
                                  .count = 1U,
                                  .maxDepth = 3U,
                                  .perStepBudget = 1U},
        ParticleEventBindingAsset{.sourceEmitterId = 12U,
                                  .trigger = ParticleEventTrigger::Birth,
                                  .action = ParticleEventAction::EmitTargetEmitter,
                                  .targetEmitterId = 13U,
                                  .count = 1U,
                                  .maxDepth = 3U,
                                  .perStepBudget = 1U},
        ParticleEventBindingAsset{.sourceEmitterId = 13U,
                                  .trigger = ParticleEventTrigger::Birth,
                                  .action = ParticleEventAction::EmitTargetEmitter,
                                  .targetEmitterId = 14U,
                                  .count = 1U,
                                  .maxDepth = 3U,
                                  .perStepBudget = 1U},
    };
    Require(ParticleEffectAssetValidator::ValidateStructure(locallyBoundedDepth).Succeeded(),
            "acyclic emitter graph with an unreachable depth-four action was rejected");
    rejects(
        "non-finite curve",
        [](ParticleEffectAsset& value) {
            value.emitters[0].spawn.rateOverTime.keyframes[0].value = std::numeric_limits<float>::infinity();
        },
        ParticleEffectDiagnosticCode::InvalidCurve);
    rejects(
        "capacity ceiling",
        [](ParticleEffectAsset& value) {
            value.emitters[0].maxParticles = kParticleEffectMaxCpuParticlesPerEmitter + 1U;
        },
        ParticleEffectDiagnosticCode::LimitExceeded);
    ParticleEffectAsset capacityBoundary = valid;
    for (ParticleEmitterAsset& emitter : capacityBoundary.emitters)
        emitter.maxParticles = kParticleEffectMaxCpuParticlesPerScene / kParticleEffectMaxEmitters;
    Require(ParticleEffectAssetValidator::ValidateStructure(capacityBoundary).Succeeded(),
            "combined CPU scene capacity boundary was rejected");
    ++capacityBoundary.emitters.back().maxParticles;
    Require(HasDiagnostic(ParticleEffectAssetValidator::ValidateStructure(capacityBoundary).diagnostics,
                          ParticleEffectDiagnosticCode::LimitExceeded, "effect.emitterCapacity"),
            "combined CPU scene capacity boundary plus one was accepted");
    static_assert(kParticleEffectMaxInstancesPerScene == 256U);
    rejects(
        "output payload", [](ParticleEffectAsset& value) { value.emitters[0].output.payload = ParticleMeshOutput{}; },
        ParticleEffectDiagnosticCode::InvalidValue);
    rejects(
        "backend enum",
        [](ParticleEffectAsset& value) { value.backendPolicy = static_cast<ParticleBackendPolicy>(255U); },
        ParticleEffectDiagnosticCode::InvalidEnum);
    rejects(
        "module enum",
        [](ParticleEffectAsset& value) { value.emitters[0].modules[0].type = static_cast<ParticleModuleType>(255U); },
        ParticleEffectDiagnosticCode::InvalidEnum);
    rejects(
        "output enum",
        [](ParticleEffectAsset& value) { value.emitters[0].output.blend = static_cast<ParticleBlendMode>(255U); },
        ParticleEffectDiagnosticCode::InvalidEnum);
    rejects(
        "event enum",
        [](ParticleEffectAsset& value) { value.eventBindings[0].trigger = static_cast<ParticleEventTrigger>(255U); },
        ParticleEffectDiagnosticCode::InvalidEnum);
    rejects(
        "easing enum",
        [](ParticleEffectAsset& value) {
            value.emitters[0].spawn.rateOverTime.keyframes[0].easing = static_cast<kb::math::Easing>(255U);
        },
        ParticleEffectDiagnosticCode::InvalidCurve);
}

void RunBoundedFuzzTest() {
    const std::filesystem::path fixture =
        std::filesystem::path{KB_PARTICLE_ASSET_TEST_SOURCE_DIR} / "fixtures" / "CanonicalParticleEffectV2.kbvfx";
    const std::string canonical = ReadText(fixture);
    std::uint32_t state = 0x5A17C9E3U;
    for (std::uint32_t iteration = 0U; iteration < 512U; ++iteration) {
        state = state * 1664525U + 1013904223U;
        std::string candidate = canonical;
        const std::size_t edits = 1U + (state % 8U);
        for (std::size_t edit = 0U; edit < edits; ++edit) {
            state = state * 1664525U + 1013904223U;
            const std::size_t position = state % candidate.size();
            candidate[position] = static_cast<char>(32U + ((state >> 8U) % 95U));
        }
        Require(candidate.size() <= kb::scene::kParticleEffectMaxSourceBytes,
                "fuzz case exceeded the parser input contract");
        static_cast<void>(kb::scene::ParticleEffectAssetIO::Parse(candidate));
    }

    for (std::string_view countPath :
         {"effect.emitterCount", "effect.emitter[0].spawn.rate.keyCount", "effect.emitter[0].spawn.burstCount",
          "effect.emitter[0].moduleCount", "effect.eventBindingCount"}) {
        std::string hostileCount = canonical;
        const std::size_t value = hostileCount.find(' ', hostileCount.find(countPath));
        const std::size_t lineEnd = hostileCount.find('\n', value);
        Require(value != std::string::npos && lineEnd != std::string::npos,
                "hostile count mutation source was not found");
        hostileCount.replace(value + 1U, lineEnd - value - 1U, "4294967295");
        Require(HasDiagnostic(kb::scene::ParticleEffectAssetIO::Parse(hostileCount).diagnostics,
                              kb::scene::ParticleEffectDiagnosticCode::LimitExceeded, countPath),
                "UINT32_MAX count did not fail through its schema bound");
    }

    std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;
    std::string comprehensive = *kb::scene::ParticleEffectAssetIO::Serialize(MakeComprehensiveAsset(), diagnostics);
    ReplaceOnce(comprehensive, "effect.emitter[0].module[4].payload.gradient.stopCount 2",
                "effect.emitter[0].module[4].payload.gradient.stopCount 4294967295");
    Require(HasDiagnostic(kb::scene::ParticleEffectAssetIO::Parse(comprehensive).diagnostics,
                          kb::scene::ParticleEffectDiagnosticCode::LimitExceeded,
                          "effect.emitter[0].module[4].payload.gradient.stopCount"),
            "UINT32_MAX gradient count did not fail through its schema bound");

    std::string manyRecords = "21kb ParticleEffect 2\n";
    for (std::size_t index = 0U; index <= kb::scene::kParticleEffectMaxRecords; ++index)
        manyRecords += "x" + std::to_string(index) + " 0\n";
    Require(manyRecords.size() <= kb::scene::kParticleEffectMaxSourceBytes,
            "unique-record adversarial source does not fit the source cap");
    Require(HasDiagnostic(kb::scene::ParticleEffectAssetIO::Parse(manyRecords).diagnostics,
                          kb::scene::ParticleEffectDiagnosticCode::LimitExceeded),
            "unique-record adversarial source exceeded the bounded record allocation without rejection");
}

void RunAtomicWriteFailureTest() {
    using namespace kb::scene;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_particle_asset_atomic_test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    const std::filesystem::path destination = root / "Preserved.kbvfx";
    std::filesystem::create_directories(root, error);
    Require(!error, "atomic test directory could not be prepared");
    WriteText(destination, "preserved destination bytes");
    std::filesystem::path tempPath = destination;
    tempPath += ".tmp";
    std::filesystem::create_directories(tempPath, error);
    Require(!error, "atomic test temporary conflict could not be prepared");
    const std::filesystem::path sentinel = tempPath / "sentinel.txt";
    WriteText(sentinel, "preserved temporary bytes");
    const ParticleEffectSaveResult result = ParticleEffectAssetIO::SaveDetailed(destination, MakeComprehensiveAsset());
    Require(!result.Succeeded() && HasDiagnostic(result.diagnostics, ParticleEffectDiagnosticCode::AtomicWriteFailed),
            "atomic replacement failure was not reported");
    Require(ReadText(destination) == "preserved destination bytes",
            "failed atomic replacement modified existing destination content");
    Require(std::filesystem::is_directory(tempPath) && ReadText(sentinel) == "preserved temporary bytes",
            "failed atomic replacement changed the prepared temporary conflict");

    const std::filesystem::path first = root / "First.kbvfx";
    const std::filesystem::path second = root / "Second.kbvfx";
    Require(ParticleEffectAssetIO::Save(first, MakeComprehensiveAsset()), "first stable save failed");
    const ParticleEffectLoadResult loaded = ParticleEffectAssetIO::LoadDetailed(first);
    Require(loaded.Succeeded() && ParticleEffectAssetIO::Save(second, *loaded.asset),
            "load-save stability setup failed");
    Require(ReadText(first) == ReadText(second), "save-load-save was not byte stable");
}

[[nodiscard]] kb::assets::AssetMetadata DependencyMetadata(std::uint64_t id, std::string type,
                                                           std::filesystem::path virtualPath,
                                                           std::filesystem::path physicalPath = {}) {
    return kb::assets::AssetMetadata{.id = kb::assets::AssetId{id},
                                     .type = std::move(type),
                                     .virtualPath = std::move(virtualPath),
                                     .physicalPath = std::move(physicalPath)};
}

[[nodiscard]] kb::scene::ParticleEffectAsset MakeDependencyEffect() {
    using namespace kb::scene;
    ParticleEffectAsset asset = MakeComprehensiveAsset();
    asset.eventBindings.clear();
    asset.emitters.front().modules.pop_back();
    for (ParticleEmitterAsset& emitter : asset.emitters) {
        emitter.output.material = ParticleAssetReference{.assetId = 101U};
        if (!emitter.output.mesh.Empty())
            emitter.output.mesh = ParticleAssetReference{.assetId = 102U};
        if (!emitter.output.textureAtlas.Empty())
            emitter.output.textureAtlas = ParticleAssetReference{.assetId = 103U};
    }
    return asset;
}

void AddExternalEffectBinding(kb::scene::ParticleEffectAsset& asset, kb::scene::ParticleAssetReference reference) {
    using namespace kb::scene;
    asset.eventBindings.push_back(ParticleEventBindingAsset{.sourceEmitterId = 10U,
                                                            .trigger = ParticleEventTrigger::Death,
                                                            .sourceModuleId = 1U,
                                                            .action = ParticleEventAction::EmitEffectAsset,
                                                            .targetEffect = std::move(reference),
                                                            .count = 1U,
                                                            .maxDepth = 2U,
                                                            .perStepBudget = 16U});
}

void RunDependencyValidationTest() {
    using namespace kb::scene;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_particle_dependency_test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    Require(!error, "dependency test directory could not be prepared");

    kb::assets::AssetRegistry registry;
    Require(registry.Upsert(DependencyMetadata(101U, "RenderMaterialInstance", "/Assets/Material.kbmat")) &&
                registry.Upsert(DependencyMetadata(102U, "RenderMesh", "/Assets/Mesh.kbmesh")) &&
                registry.Upsert(DependencyMetadata(103U, "RenderTexture", "/Assets/Atlas.kbtex")),
            "dependency asset metadata registration failed");

    ParticleEffectAsset child = MakeDependencyEffect();
    child.effectId = 201U;
    child.displayName = "Child effect";
    const std::filesystem::path childPath = root / "Child.kbvfx";
    Require(ParticleEffectAssetIO::Save(childPath, child), "dependency child effect save failed");
    Require(registry.Upsert(DependencyMetadata(201U, kParticleEffectAssetType, "/Effects/Child.kbvfx", childPath)),
            "dependency child effect metadata registration failed");

    ParticleEffectAsset asset = MakeDependencyEffect();
    asset.emitters[0].output.material = {.assetId = 101U, .virtualPath = "/Assets/Material.kbmat"};
    asset.emitters[1].output.material = {.virtualPath = "/Assets/Material.kbmat"};
    AddExternalEffectBinding(asset, {.assetId = 201U, .virtualPath = "/Effects/Child.kbvfx"});
    const std::filesystem::path rootPath = root / "Root.kbvfx";
    Require(ParticleEffectAssetIO::Save(rootPath, asset), "dependency root effect save failed");
    const kb::assets::AssetMetadata rootMetadata =
        DependencyMetadata(200U, kParticleEffectAssetType, "/Effects/Root.kbvfx", rootPath);
    Require(registry.Upsert(rootMetadata), "dependency root effect metadata registration failed");

    ParticleEffectAssetLoader loader;
    ParticleEffectDependencyResult analysis =
        ParticleEffectAssetValidator::ValidateDependencies(rootMetadata, registry);
    const std::vector<std::uint64_t> ids = [&] {
        std::vector<std::uint64_t> output;
        for (kb::assets::AssetId id : analysis.dependencies)
            output.push_back(id.value);
        return output;
    }();
    Require(analysis.Succeeded() && ids == std::vector<std::uint64_t>({101U, 102U, 103U, 201U}),
            "dependency discovery was incomplete, duplicated, or non-deterministic");
    Require(loader.DiscoverDependencies(rootMetadata, registry) == analysis.dependencies &&
                !loader.ValidateDependencies(rootMetadata, registry).has_value(),
            "loader dependency hooks diverged from shared analysis");
    Require(asset.emitters[0].output.material.assetId == 101U &&
                asset.emitters[0].output.material.virtualPath == "/Assets/Material.kbmat" &&
                asset.eventBindings[0].targetEffect.assetId == 201U &&
                asset.eventBindings[0].targetEffect.virtualPath == "/Effects/Child.kbvfx",
            "dependency validation mutated authored asset references");

    const auto rejects = [&](std::string_view label, auto mutate, ParticleEffectDiagnosticCode expected,
                             std::string_view path, ParticleStableId emitterId = 10U, ParticleStableId moduleId = 0U) {
        ParticleEffectAsset candidate = asset;
        mutate(candidate);
        Require(ParticleEffectAssetIO::Save(rootPath, candidate), "invalid dependency case could not be authored");
        const ParticleEffectDependencyResult rejected =
            ParticleEffectAssetValidator::ValidateDependencies(rootMetadata, registry);
        const auto found = std::find_if(rejected.diagnostics.begin(), rejected.diagnostics.end(),
                                        [&](const ParticleEffectDiagnostic& diagnostic) {
                                            return diagnostic.code == expected && diagnostic.propertyPath == path;
                                        });
        Require(found != rejected.diagnostics.end() && found->line != 0U && found->emitterId == emitterId &&
                    found->moduleId == moduleId,
                std::string{"dependency validation case lacked precise context: "} + std::string{label});
    };
    rejects(
        "both mismatch",
        [](ParticleEffectAsset& value) { value.emitters[0].output.material.virtualPath = "/Assets/Atlas.kbtex"; },
        ParticleEffectDiagnosticCode::MismatchedReference, "effect.emitter[0].output.material");
    rejects(
        "missing", [](ParticleEffectAsset& value) { value.emitters[0].output.material.assetId = 999U; },
        ParticleEffectDiagnosticCode::MissingDependency, "effect.emitter[0].output.material.assetId");
    rejects(
        "wrong type", [](ParticleEffectAsset& value) { value.emitters[0].output.material = {.assetId = 103U}; },
        ParticleEffectDiagnosticCode::WrongAssetType, "effect.emitter[0].output.material");
    rejects(
        "direct cycle", [](ParticleEffectAsset& value) { value.eventBindings[0].targetEffect = {.assetId = 200U}; },
        ParticleEffectDiagnosticCode::CyclicReference, "effect.eventBinding[0].targetEffect", 10U, 1U);

    ParticleEffectAsset indirect = child;
    AddExternalEffectBinding(indirect, {.assetId = 202U});
    Require(ParticleEffectAssetIO::Save(childPath, indirect), "indirect cycle child save failed");
    ParticleEffectAsset third = child;
    third.effectId = 202U;
    third.displayName = "Third effect";
    AddExternalEffectBinding(third, {.assetId = 200U});
    const std::filesystem::path thirdPath = root / "Third.kbvfx";
    Require(ParticleEffectAssetIO::Save(thirdPath, third) &&
                registry.Upsert(DependencyMetadata(202U, kParticleEffectAssetType, "/Effects/Third.kbvfx", thirdPath)),
            "indirect cycle third effect setup failed");
    Require(ParticleEffectAssetIO::Save(rootPath, asset), "indirect cycle root restore failed");
    analysis = ParticleEffectAssetValidator::ValidateDependencies(rootMetadata, registry);
    Require(HasDiagnostic(analysis.diagnostics, ParticleEffectDiagnosticCode::CyclicReference,
                          "effect.eventBinding[0].targetEffect"),
            "indirect external dependency cycle was accepted");
    const std::vector<kb::assets::AssetId> directEdges = loader.DiscoverDependencies(rootMetadata, registry);
    Require(std::find(directEdges.begin(), directEdges.end(), kb::assets::AssetId{201U}) != directEdges.end() &&
                std::find(directEdges.begin(), directEdges.end(), kb::assets::AssetId{202U}) == directEdges.end(),
            "root dependency discovery published a transitive edge instead of direct edges only");

    ParticleEffectAsset self = child;
    self.eventBindings.clear();
    AddExternalEffectBinding(self, {.assetId = 201U});
    Require(ParticleEffectAssetIO::Save(childPath, self), "self-cycle setup failed");
    analysis = ParticleEffectAssetValidator::ValidateDependencies(rootMetadata, registry);
    Require(HasDiagnostic(analysis.diagnostics, ParticleEffectDiagnosticCode::CyclicReference,
                          "effect.eventBinding[0].targetEffect"),
            "nested self external dependency cycle was accepted");

    kb::assets::AssetRegistry boundedRegistry;
    Require(boundedRegistry.Upsert(DependencyMetadata(101U, "RenderMaterial", "/Assets/Material.kbmat")) &&
                boundedRegistry.Upsert(DependencyMetadata(102U, "RenderMesh", "/Assets/Mesh.kbmesh")) &&
                boundedRegistry.Upsert(DependencyMetadata(103U, "RenderTexture", "/Assets/Atlas.kbtex")),
            "bounded dependency resource registration failed");
    kb::assets::AssetMetadata boundedRoot;
    for (std::size_t index = 0U; index < kParticleEffectMaxDependencyAssets; ++index) {
        ParticleEffectAsset node = MakeDependencyEffect();
        node.effectId = 1'000U + index;
        node.displayName = "Bounded dependency " + std::to_string(index);
        node.emitters.resize(1U);
        node.eventBindings.clear();
        if (index + 1U < kParticleEffectMaxDependencyAssets)
            AddExternalEffectBinding(node, {.assetId = 1'001U + index});
        const std::filesystem::path path = root / ("Bounded" + std::to_string(index) + ".kbvfx");
        Require(ParticleEffectAssetIO::Save(path, node), "bounded dependency node save failed");
        kb::assets::AssetMetadata metadata = DependencyMetadata(
            1'000U + index, kParticleEffectAssetType, "/Effects/Bounded" + std::to_string(index) + ".kbvfx", path);
        Require(boundedRegistry.Upsert(metadata), "bounded dependency metadata registration failed");
        if (index == 0U)
            boundedRoot = metadata;
    }
    const ParticleEffectDependencyResult dependencyBoundary =
        ParticleEffectAssetValidator::ValidateDependencies(boundedRoot, boundedRegistry);
    Require(dependencyBoundary.Succeeded(),
            dependencyBoundary.diagnostics.empty()
                ? "external dependency graph hard-limit boundary was rejected"
                : FormatParticleEffectDiagnostic(dependencyBoundary.diagnostics.front()));
    ParticleEffectAsset last = MakeDependencyEffect();
    last.effectId = 1'000U + kParticleEffectMaxDependencyAssets - 1U;
    last.displayName = "Bounded dependency last";
    last.emitters.resize(1U);
    last.eventBindings.clear();
    AddExternalEffectBinding(last, {.assetId = 1'000U + kParticleEffectMaxDependencyAssets});
    const std::filesystem::path lastPath =
        root / ("Bounded" + std::to_string(kParticleEffectMaxDependencyAssets - 1U) + ".kbvfx");
    Require(ParticleEffectAssetIO::Save(lastPath, last), "dependency boundary plus one link save failed");
    ParticleEffectAsset overflow = last;
    overflow.effectId = 1'000U + kParticleEffectMaxDependencyAssets;
    overflow.displayName = "Bounded dependency overflow";
    overflow.eventBindings.clear();
    const std::filesystem::path overflowPath = root / "BoundedOverflow.kbvfx";
    Require(ParticleEffectAssetIO::Save(overflowPath, overflow) &&
                boundedRegistry.Upsert(DependencyMetadata(1'000U + kParticleEffectMaxDependencyAssets,
                                                          kParticleEffectAssetType, "/Effects/BoundedOverflow.kbvfx",
                                                          overflowPath)),
            "dependency boundary plus one node setup failed");
    Require(HasDiagnostic(ParticleEffectAssetValidator::ValidateDependencies(boundedRoot, boundedRegistry).diagnostics,
                          ParticleEffectDiagnosticCode::LimitExceeded, "effect"),
            "external dependency graph boundary plus one was accepted");
}

void RunRecipeAssetTest() {
    using namespace kb::scene;
    struct RecipeExpectation {
        std::string_view fileName;
        std::string_view displayName;
        std::string_view category;
    };
    static constexpr RecipeExpectation recipes[] = {
        {"BloodSplatter", "Blood Splatter", "Hit Impact"},
        {"MuzzleFlash", "Muzzle Flash", "Projectile"},
        {"BulletTrail", "Bullet Trail", "Trail"},
        {"Explosion", "Explosion", "Hit Impact"},
        {"ImpactSparks", "Impact Sparks", "Hit Impact"},
        {"Rain", "Rain", "Rain"},
        {"Snow", "Snow", "Others"},
        {"Leaves", "Leaves", "Others"},
        {"FireBurst", "Fire Burst", "Simple"},
        {"FrostNova", "Frost Nova", "Aura"},
        {"ArcaneSparks", "Arcane Sparks", "Simple"},
        {"CoinBurst", "Coin Burst", "Simple"},
        {"LevelUpAura", "Level Up Aura", "Aura"},
        {"SmokeStack", "Smoke Stack", "Others"},
        {"SparksShower", "Sparks Shower", "Others"},
    };
    static_assert(std::size(recipes) == 15U);
    const std::filesystem::path recipeRoot{KB_21KB_PARTICLE_RECIPE_DIR};
    kb::assets::AssetRegistry registry;
    Require(
        registry.Upsert(DependencyMetadata(500U, "RenderMaterial", "/21kbParticle/Materials/DefaultParticle.kbmat")),
        "recipe material dependency registration failed");
    ParticleEffectAssetLoader loader;
    std::set<std::string> categories;
    for (std::size_t index = 0U; index < std::size(recipes); ++index) {
        const RecipeExpectation& recipe = recipes[index];
        const std::filesystem::path path = recipeRoot / (std::string{recipe.fileName} + ".kbvfx");
        const ParticleEffectLoadResult loaded = ParticleEffectAssetIO::LoadDetailed(path);
        Require(loaded.Succeeded() && ParticleEffectAssetValidator::ValidateStructure(*loaded.asset).Succeeded(),
                "recipe did not load and structurally validate without diagnostics");
        Require(loaded.asset->displayName == recipe.displayName && loaded.asset->recipeCategory == recipe.category,
                "recipe display name or browser category differs from the canonical mapping");
        categories.insert(loaded.asset->recipeCategory);
        const kb::assets::AssetMetadata metadata =
            DependencyMetadata(600U + index, kParticleEffectAssetType,
                               "/21kbParticle/Recipes/" + std::string{recipe.fileName} + ".kbvfx", path);
        Require(registry.Upsert(metadata), "recipe metadata registration failed");
        const ParticleEffectDependencyResult dependencies =
            ParticleEffectAssetValidator::ValidateDependencies(metadata, registry);
        Require(dependencies.Succeeded() && dependencies.dependencies == std::vector<kb::assets::AssetId>{{500U}},
                "recipe dependency tree was incomplete or invalid");
    }
    Require(categories.size() >= 4U, "recipe categories are not meaningfully varied");
}

} // namespace

int main() {
    try {
        RunCanonicalGoldenTest();
        RunComprehensiveSchemaTest();
        RunHardLimitBoundaryTest();
        RunBoundedFileLoadTest();
        RunLegacyMigrationTest();
        RunMalformedInputTest();
        RunValidatorMatrixTest();
        RunBoundedFuzzTest();
        RunAtomicWriteFailureTest();
        RunDependencyValidationTest();
        RunRecipeAssetTest();
        std::cout << "21kb Particle System asset tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "21kb Particle System asset tests failed: " << error.what() << '\n';
        return 1;
    }
}
