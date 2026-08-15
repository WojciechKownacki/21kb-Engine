#include "ParticleEmitterInspectorModel.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"

#include <algorithm>
#include <charconv>
#include <iterator>
#include <limits>
#include <sstream>
#include <system_error>

namespace kb::particle_editor {
namespace {

[[nodiscard]] const char* OutputName(kb::scene::ParticleOutputType type) noexcept {
    switch (type) {
    case kb::scene::ParticleOutputType::Billboard: return "Billboard";
    case kb::scene::ParticleOutputType::StretchedBillboard: return "Stretched Billboard";
    case kb::scene::ParticleOutputType::PointSprite: return "Point Sprite";
    case kb::scene::ParticleOutputType::Mesh: return "Mesh";
    case kb::scene::ParticleOutputType::Trail: return "Trail";
    case kb::scene::ParticleOutputType::Ribbon: return "Ribbon";
    case kb::scene::ParticleOutputType::Beam: return "Beam";
    case kb::scene::ParticleOutputType::Volumetric: return "Volumetric";
    }
    return "Invalid";
}

[[nodiscard]] const char* ModuleName(kb::scene::ParticleModuleType type) noexcept {
    switch (type) {
    case kb::scene::ParticleModuleType::InitialVelocity: return "Initial Velocity";
    case kb::scene::ParticleModuleType::Gravity: return "Gravity";
    case kb::scene::ParticleModuleType::Wind: return "Wind";
    case kb::scene::ParticleModuleType::Drag: return "Drag";
    case kb::scene::ParticleModuleType::ColorOverLife: return "Color Over Life";
    case kb::scene::ParticleModuleType::SizeOverLife: return "Size Over Life";
    case kb::scene::ParticleModuleType::AlphaOverLife: return "Alpha Over Life";
    case kb::scene::ParticleModuleType::CollisionPlane: return "Collision Plane";
    case kb::scene::ParticleModuleType::SubEmitter: return "Sub Emitter";
    }
    return "Invalid";
}

[[nodiscard]] std::string ModuleSummary(const kb::scene::ParticleModuleAsset& module) {
    if (const auto* value = std::get_if<kb::scene::ParticleColorOverLifeModule>(&module.payload))
        return std::to_string(value->gradient.stops.size()) + " gradient stops";
    if (const auto* value = std::get_if<kb::scene::ParticleSizeOverLifeModule>(&module.payload))
        return std::to_string(value->curve.keyframes.size()) + " size keys";
    if (const auto* value = std::get_if<kb::scene::ParticleAlphaOverLifeModule>(&module.payload))
        return std::to_string(value->curve.keyframes.size()) + " alpha keys";
    return module.enabled ? "Enabled" : "Disabled";
}

[[nodiscard]] std::string VecText(const kb::math::Vec3& value) {
    std::ostringstream out;
    out << value.x << ' ' << value.y << ' ' << value.z;
    return out.str();
}

template <typename T>
[[nodiscard]] std::string ScalarText(T value) { return std::to_string(value); }

// Round-trip-safe and locale-independent (std::to_chars never consults the process locale), unlike
// std::to_string(float)'s fixed 6-decimal truncation: two keyframe/stop times that differ only past
// the 6th decimal would otherwise print identically, and the field-separator ',' this format uses
// would itself corrupt under a comma-decimal locale. Mirrors ParticleEffectAssetIO.cpp's Float().
[[nodiscard]] std::string FloatText(float value) {
    char buffer[64]{};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general,
                                      std::numeric_limits<float>::max_digits10);
    return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string{};
}

[[nodiscard]] std::string CurveText(const kb::math::Curve& curve) {
    std::string text;
    for (std::size_t index = 0U; index < curve.keyframes.size(); ++index) {
        if (index != 0U) text += ';';
        const auto& key = curve.keyframes[index];
        text += FloatText(key.time) + ',' + FloatText(key.value) + ',' +
            std::to_string(static_cast<std::uint32_t>(key.easing));
    }
    return text;
}

[[nodiscard]] std::string GradientText(const kb::math::Gradient& gradient) {
    std::string text;
    for (std::size_t index = 0U; index < gradient.stops.size(); ++index) {
        if (index != 0U) text += ';';
        const auto& stop = gradient.stops[index];
        text += FloatText(stop.time) + ',' + FloatText(stop.color.r) + ',' +
            FloatText(stop.color.g) + ',' + FloatText(stop.color.b) + ',' + FloatText(stop.color.a);
    }
    return text;
}

void AddModuleProperties(ParticleEmitterInspectorView& view, const kb::scene::ParticleModuleAsset& module) {
    std::uint8_t field = 0U;
    const auto add = [&](std::string label, std::string value, bool editable = true) {
        view.properties.push_back({.property = ParticleEditorProperty::ModulePayload, .moduleId = module.moduleId,
                                   .payloadField = field++,
                                   .label = std::move(label), .value = std::move(value), .editable = editable});
    };
    std::visit([&](const auto& payload) {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, kb::scene::ParticleInitialVelocityModule>) {
            add("Direction", VecText(payload.direction)); add("Speed min", ScalarText(payload.speedMin));
            add("Speed max", ScalarText(payload.speedMax)); add("Randomization", ScalarText(payload.randomization));
            add("Spread", ScalarText(payload.spreadDegrees));
        } else if constexpr (std::is_same_v<T, kb::scene::ParticleGravityModule>) {
            add("Acceleration", VecText(payload.acceleration)); add("Scene gravity scale", ScalarText(payload.sceneGravityScale));
        } else if constexpr (std::is_same_v<T, kb::scene::ParticleWindModule>) add("Acceleration", VecText(payload.acceleration));
        else if constexpr (std::is_same_v<T, kb::scene::ParticleDragModule>) add("Coefficient", ScalarText(payload.coefficient));
        else if constexpr (std::is_same_v<T, kb::scene::ParticleColorOverLifeModule>)
            add("Gradient", GradientText(payload.gradient));
        else if constexpr (std::is_same_v<T, kb::scene::ParticleSizeOverLifeModule>)
            add("Curve", CurveText(payload.curve));
        else if constexpr (std::is_same_v<T, kb::scene::ParticleAlphaOverLifeModule>)
            add("Curve", CurveText(payload.curve));
        else if constexpr (std::is_same_v<T, kb::scene::ParticleCollisionPlaneModule>) {
            add("Normal", VecText(payload.normal)); add("Distance", ScalarText(payload.distance));
            add("Restitution", ScalarText(payload.restitution)); add("Friction", ScalarText(payload.friction));
            add("Max events", ScalarText(payload.maxEventsPerStep));
        } else if constexpr (std::is_same_v<T, kb::scene::ParticleSubEmitterModule>) {
            add("Target emitter", ScalarText(payload.targetEmitterId));
            add("Trigger", ScalarText(static_cast<std::uint32_t>(payload.trigger)));
            add("Count", ScalarText(payload.count)); add("Max depth", ScalarText(payload.maxDepth));
        }
    }, module.payload);
}

} // namespace

ParticleEmitterInspectorView ParticleEmitterInspectorModel::Build(
    const kb::scene::ParticleEffectAsset& asset,
    kb::scene::ParticleStableId selectedEmitterId,
    kb::scene::ParticleStableId selectedModuleId,
    const kb::assets::AssetMetadata* owner,
    const kb::assets::AssetRegistry* registry,
    const kb::particle_plugin::ParticleCompileRequest& request) {
    ParticleEmitterInspectorView view;
    view.emitterId = selectedEmitterId;
    view.outputChoices.reserve(8U);
    for (std::uint8_t raw = 0U; raw <= static_cast<std::uint8_t>(kb::scene::ParticleOutputType::Volumetric); ++raw) {
        const auto type = static_cast<kb::scene::ParticleOutputType>(raw);
        auto candidate = asset;
        const auto emitter = std::find_if(candidate.emitters.begin(), candidate.emitters.end(),
            [selectedEmitterId](const auto& value) { return value.emitterId == selectedEmitterId; });
        if (emitter != candidate.emitters.end()) {
            emitter->output.type = type;
            emitter->output.payload = kb::scene::DefaultParticleOutputPayload(type);
        }
        auto diagnostics = kb::particle_plugin::ParticleEffectCompiler::ValidateCapabilities(candidate, request);
        std::erase_if(diagnostics, [selectedEmitterId](const auto& diagnostic) {
            return diagnostic.emitterId != selectedEmitterId ||
                   diagnostic.propertyPath.find(".output.type") == std::string::npos;
        });
        view.outputChoices.push_back({.type = type, .label = OutputName(type),
                                      .enabled = diagnostics.empty(), .diagnostics = std::move(diagnostics)});
    }
    const auto selected = std::find_if(asset.emitters.begin(), asset.emitters.end(),
        [selectedEmitterId](const auto& value) { return value.emitterId == selectedEmitterId; });
    if (selected != asset.emitters.end()) {
        const auto& spawn = selected->spawn;
        const auto addProperty = [&](ParticleEditorProperty property, std::string label, std::string value,
                                     bool editable = true) {
            view.properties.push_back({.property = property, .label = std::move(label),
                                       .value = std::move(value), .editable = editable});
        };
        addProperty(ParticleEditorProperty::SpawnRateCurve, "Rate curve", CurveText(spawn.rateOverTime));
        addProperty(ParticleEditorProperty::SpawnBurstsSummary, "Bursts",
            std::to_string(spawn.bursts.size()) + " entries", false);
        addProperty(ParticleEditorProperty::SpawnLifetimeMin, "Lifetime min", ScalarText(spawn.lifetimeMin));
        addProperty(ParticleEditorProperty::SpawnLifetimeMax, "Lifetime max", ScalarText(spawn.lifetimeMax));
        addProperty(ParticleEditorProperty::SpawnSpeedMin, "Speed min", ScalarText(spawn.speedMin));
        addProperty(ParticleEditorProperty::SpawnSpeedMax, "Speed max", ScalarText(spawn.speedMax));
        addProperty(ParticleEditorProperty::SpawnDirection, "Direction", VecText(spawn.direction));
        addProperty(ParticleEditorProperty::SpawnSpreadDegrees, "Spread", ScalarText(spawn.spreadDegrees));
        addProperty(ParticleEditorProperty::SpawnRandomization, "Randomization", ScalarText(spawn.randomization));
        addProperty(ParticleEditorProperty::SpawnPrewarmSeconds, "Prewarm", ScalarText(spawn.prewarmSeconds));
        addProperty(ParticleEditorProperty::OutputBlend, "Blend", ScalarText(static_cast<std::uint32_t>(selected->output.blend)));
        addProperty(ParticleEditorProperty::OutputSort, "Sort", ScalarText(static_cast<std::uint32_t>(selected->output.sort)));
        addProperty(ParticleEditorProperty::OutputDepthTest, "Depth test", selected->output.depthTest ? "true" : "false");
        addProperty(ParticleEditorProperty::OutputDepthWrite, "Depth write", selected->output.depthWrite ? "true" : "false");
        addProperty(ParticleEditorProperty::OutputSoftParticles, "Soft particles", selected->output.softParticles ? "true" : "false");
        addProperty(ParticleEditorProperty::OutputAntiAliasing, "Anti-aliasing", selected->output.antiAliasing ? "true" : "false");
        addProperty(ParticleEditorProperty::OutputAlignment, "Alignment", ScalarText(static_cast<std::uint32_t>(selected->output.alignment)));
        std::visit([&](const auto& payload) {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, kb::scene::ParticleBillboardOutput> ||
                          std::is_same_v<T, kb::scene::ParticleStretchedBillboardOutput> ||
                          std::is_same_v<T, kb::scene::ParticlePointSpriteOutput>) {
                addProperty(ParticleEditorProperty::FlipbookColumns, "Atlas columns", ScalarText(payload.flipbook.columns));
                addProperty(ParticleEditorProperty::FlipbookRows, "Atlas rows", ScalarText(payload.flipbook.rows));
                addProperty(ParticleEditorProperty::FlipbookFramesPerSecond, "Frame rate", ScalarText(payload.flipbook.framesPerSecond));
                addProperty(ParticleEditorProperty::FlipbookLooping, "Flipbook loop", payload.flipbook.looping ? "true" : "false");
                if constexpr (std::is_same_v<T, kb::scene::ParticleStretchedBillboardOutput>) {
                    addProperty(ParticleEditorProperty::OutputVelocityScale, "Velocity scale", ScalarText(payload.velocityScale));
                    addProperty(ParticleEditorProperty::OutputMinimumLength, "Minimum length", ScalarText(payload.minimumLength));
                } else if constexpr (std::is_same_v<T, kb::scene::ParticlePointSpriteOutput>)
                    addProperty(ParticleEditorProperty::OutputPointDiameter, "Point diameter", ScalarText(payload.diameter));
            }
        }, selected->output.payload);
        view.modules.reserve(selected->modules.size());
        for (const auto& module : selected->modules) {
            view.modules.push_back({.moduleId = module.moduleId, .authoringOrder = module.authoringOrder,
                .type = module.type, .label = ModuleName(module.type), .summary = ModuleSummary(module),
                .enabled = module.enabled, .selected = module.moduleId == selectedModuleId});
            AddModuleProperties(view, module);
        }
        std::sort(view.modules.begin(), view.modules.end(), [](const auto& left, const auto& right) {
            return left.authoringOrder < right.authoringOrder;
        });
    }
    view.diagnostics = kb::scene::ParticleEffectAssetValidator::ValidateStructure(asset).diagnostics;
    auto capabilities = kb::particle_plugin::ParticleEffectCompiler::ValidateCapabilities(asset, request);
    view.diagnostics.insert(view.diagnostics.end(), std::make_move_iterator(capabilities.begin()),
                            std::make_move_iterator(capabilities.end()));
    if (owner != nullptr && registry != nullptr) {
        auto dependency = kb::scene::ParticleEffectAssetValidator::ValidateDependencies(asset, *owner, *registry);
        view.diagnostics.insert(view.diagnostics.end(), std::make_move_iterator(dependency.diagnostics.begin()),
                                std::make_move_iterator(dependency.diagnostics.end()));
        view.dependencies.reserve(dependency.transitiveDependencies.size());
        for (const kb::assets::AssetId id : dependency.transitiveDependencies) {
            const kb::assets::AssetMetadata* metadata = registry->Find(id);
            view.dependencies.push_back({.assetId = id,
                .virtualPath = metadata == nullptr ? std::string{} : metadata->virtualPath.generic_string()});
        }
    }
    return view;
}

} // namespace kb::particle_editor
