#include "ParticleEmitterInspectorModel.hpp"

#include "ParticleEditorText.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/library/EngineLibraryParsing.hpp"
#include "engine/particles/ParticleRenderSnapshot.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <sstream>
#include <system_error>

namespace kb::particle_editor {
std::optional<ParticleEmitterExecutionView> ParticleEmitterInspectorModel::ReadExecution(
    const kb::particles::ParticleRenderSnapshot* snapshot, std::uint64_t effectAssetId,
    kb::scene::ParticleStableId emitterId) {
    if (snapshot == nullptr || snapshot->IsTombstone() || effectAssetId == 0U || emitterId == 0U) return std::nullopt;
    for (const auto& emitter : snapshot->Emitters()) {
        if (emitter.effectAssetId != effectAssetId || emitter.emitterId != emitterId) continue;
        return ParticleEmitterExecutionView{.path = emitter.simulationPath, .reason = emitter.gpuFallbackReason,
            .snapshotRevision = snapshot->Revision(), .cpuRenderRecords = emitter.particleCount,
            .cpuRenderBytes = static_cast<std::uint64_t>(emitter.particleCount) * sizeof(kb::particles::ParticleRenderRecord)};
    }
    return std::nullopt;
}

std::array<std::string, 3U> ParticleEmitterInspectorModel::ExecutionRows(const ParticleEmitterInspectorView& view) {
    const std::string unavailable{ParticleText("particle.execution.unavailable", "Unavailable")};
    std::string simulation = unavailable;
    std::string records = unavailable;
    if (view.execution) {
        simulation = view.execution->path == kb::particles::ParticleEmitterSimulationPath::Gpu ? "GPU" : "CPU";
        if (view.execution->reason != kb::particles::ParticleGpuFallbackReason::None) {
            simulation += " (";
            simulation += kb::particles::ParticleGpuFallbackReasonName(view.execution->reason);
            simulation += ')';
        }
        records = ParticleFormat("particle.execution.records", "{count} ({bytes} bytes)", {
            {"count", std::to_string(view.execution->cpuRenderRecords)},
            {"bytes", std::to_string(view.execution->cpuRenderBytes)}});
    }
    return {
        ParticleFormat("particle.execution.simulation", "Simulation: {value}", {{"value", simulation}}),
        ParticleFormat("particle.execution.cpu_records", "CPU render records: {value}", {{"value", records}}),
        ParticleFormat("particle.execution.timings", "CPU/GPU timings: {value}", {{"value", unavailable}}),
    };
}

namespace {

[[nodiscard]] std::string_view OutputName(kb::scene::ParticleOutputType type) {
    switch (type) {
    case kb::scene::ParticleOutputType::Billboard: return ParticleText("particle.output.billboard", "Billboard");
    case kb::scene::ParticleOutputType::StretchedBillboard: return ParticleText("particle.output.stretched_billboard", "Stretched Billboard");
    case kb::scene::ParticleOutputType::PointSprite: return ParticleText("particle.output.point_sprite", "Point Sprite");
    case kb::scene::ParticleOutputType::Mesh: return ParticleText("particle.output.mesh", "Mesh");
    case kb::scene::ParticleOutputType::Trail: return ParticleText("particle.output.trail", "Trail");
    case kb::scene::ParticleOutputType::Ribbon: return ParticleText("particle.output.ribbon", "Ribbon");
    case kb::scene::ParticleOutputType::Beam: return ParticleText("particle.output.beam", "Beam");
    case kb::scene::ParticleOutputType::Volumetric: return ParticleText("particle.output.volumetric", "Volumetric");
    }
    return ParticleText("particle.invalid", "Invalid");
}

[[nodiscard]] std::string_view ModuleName(kb::scene::ParticleModuleType type) {
    switch (type) {
    case kb::scene::ParticleModuleType::InitialVelocity: return ParticleText("particle.module.initial_velocity", "Initial Velocity");
    case kb::scene::ParticleModuleType::Gravity: return ParticleText("particle.module.gravity", "Gravity");
    case kb::scene::ParticleModuleType::Wind: return ParticleText("particle.module.wind", "Wind");
    case kb::scene::ParticleModuleType::Drag: return ParticleText("particle.module.drag", "Drag");
    case kb::scene::ParticleModuleType::ColorOverLife: return ParticleText("particle.module.color_over_life", "Color Over Life");
    case kb::scene::ParticleModuleType::SizeOverLife: return ParticleText("particle.module.size_over_life", "Size Over Life");
    case kb::scene::ParticleModuleType::AlphaOverLife: return ParticleText("particle.module.alpha_over_life", "Alpha Over Life");
    case kb::scene::ParticleModuleType::CollisionPlane: return ParticleText("particle.module.collision_plane", "Collision Plane");
    case kb::scene::ParticleModuleType::SubEmitter: return ParticleText("particle.module.sub_emitter", "Sub Emitter");
    case kb::scene::ParticleModuleType::Noise: return ParticleText("particle.module.noise", "Noise");
    case kb::scene::ParticleModuleType::AxisRotation: return ParticleText("particle.module.axis_rotation", "Axis Rotation");
    case kb::scene::ParticleModuleType::Attraction: return ParticleText("particle.module.attraction", "Attraction");
    case kb::scene::ParticleModuleType::TorusSwirl: return ParticleText("particle.module.torus_swirl", "Torus Swirl");
    case kb::scene::ParticleModuleType::Separation: return ParticleText("particle.module.separation", "Separation");
    }
    return ParticleText("particle.invalid", "Invalid");
}

[[nodiscard]] std::string ModuleSummary(const kb::scene::ParticleModuleAsset& module) {
    if (const auto* value = std::get_if<kb::scene::ParticleColorOverLifeModule>(&module.payload))
        return ParticleFormat("particle.gradient_stops", "{count} gradient stops",
            { { "count", std::to_string(value->gradient.stops.size()) } });
    if (const auto* value = std::get_if<kb::scene::ParticleSizeOverLifeModule>(&module.payload))
        return ParticleFormat("particle.size_keys", "{count} size keys",
            { { "count", std::to_string(value->curve.keyframes.size()) } });
    if (const auto* value = std::get_if<kb::scene::ParticleAlphaOverLifeModule>(&module.payload))
        return ParticleFormat("particle.alpha_keys", "{count} alpha keys",
            { { "count", std::to_string(value->curve.keyframes.size()) } });
    return std::string{ module.enabled ? ParticleText("particle.enabled", "Enabled")
                                       : ParticleText("particle.disabled", "Disabled") };
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

void AddSlider(ParticleEditorPropertyRow& row, float value, float min, float max) {
    row.widget = ParticleEditorPropertyWidget::Slider;
    row.numericValue = value;
    row.numericMin = min;
    row.numericMax = max;
}

void AddIntegerSlider(ParticleEditorPropertyRow& row, std::uint32_t value, std::uint32_t min, std::uint32_t max) {
    row.widget = ParticleEditorPropertyWidget::IntegerSlider;
    row.numericValue = static_cast<float>(value);
    row.numericMin = static_cast<float>(min);
    row.numericMax = static_cast<float>(max);
}

void AddToggle(ParticleEditorPropertyRow& row, bool value) {
    row.widget = ParticleEditorPropertyWidget::Toggle;
    row.boolValue = value;
}

void AddEnum(ParticleEditorPropertyRow& row, std::uint32_t value, std::initializer_list<std::string_view> labels) {
    row.widget = ParticleEditorPropertyWidget::Enum;
    row.enumValue = value;
    row.enumCount = 0U;
    for (const std::string_view label : labels) {
        if (row.enumCount >= row.enumLabels.size()) break;
        row.enumLabels[row.enumCount++] = label.data(); // a literal or a catalog text: both end in a null
    }
}

void AddVector(ParticleEditorPropertyRow& row, const kb::math::Vec3& value) {
    row.widget = ParticleEditorPropertyWidget::Vector;
    row.vectorValue = value;
}

void AddCurve(ParticleEditorPropertyRow& row, const kb::math::Curve& curve) {
    row.widget = ParticleEditorPropertyWidget::Curve;
    row.curve = curve;
}

void AddGradient(ParticleEditorPropertyRow& row, const kb::math::Gradient& gradient) {
    row.widget = ParticleEditorPropertyWidget::Gradient;
    row.gradient = gradient;
}

void AddColor(ParticleEditorPropertyRow& row, const kb::math::Color& color) {
    row.widget = ParticleEditorPropertyWidget::Color;
    row.colorValue = color;
}

void AddModuleProperties(ParticleEmitterInspectorView& view, const kb::scene::ParticleModuleAsset& module) {
    std::uint8_t field = 0U;
    const auto add = [&](std::string label, std::string value, bool editable = true) -> ParticleEditorPropertyRow& {
        view.properties.push_back({.property = ParticleEditorProperty::ModulePayload, .moduleId = module.moduleId,
                                   .payloadField = field++,
                                   .label = std::move(label), .value = std::move(value), .editable = editable});
        return view.properties.back();
    };
    std::visit([&](const auto& payload) {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, kb::scene::ParticleInitialVelocityModule>) {
            AddVector(add(std::string{ ParticleText("particle.property.direction", "Direction") }, VecText(payload.direction)), payload.direction);
            AddSlider(add(std::string{ ParticleText("particle.property.speed_min", "Speed min") }, ScalarText(payload.speedMin)), payload.speedMin, 0.0F, 40.0F);
            AddSlider(add(std::string{ ParticleText("particle.property.speed_max", "Speed max") }, ScalarText(payload.speedMax)), payload.speedMax, 0.0F, 40.0F);
            AddSlider(add(std::string{ ParticleText("particle.property.randomization", "Randomization") }, ScalarText(payload.randomization)), payload.randomization, 0.0F, 1.0F);
            AddSlider(add(std::string{ ParticleText("particle.property.spread", "Spread") }, ScalarText(payload.spreadDegrees)), payload.spreadDegrees, 0.0F, 180.0F);
        } else if constexpr (std::is_same_v<T, kb::scene::ParticleGravityModule>) {
            AddVector(add(std::string{ ParticleText("particle.property.acceleration", "Acceleration") }, VecText(payload.acceleration)), payload.acceleration);
            AddSlider(add(std::string{ ParticleText("particle.property.scene_gravity_scale", "Scene gravity scale") }, ScalarText(payload.sceneGravityScale)), payload.sceneGravityScale, 0.0F, 4.0F);
        } else if constexpr (std::is_same_v<T, kb::scene::ParticleWindModule>)
            AddVector(add(std::string{ ParticleText("particle.property.acceleration", "Acceleration") }, VecText(payload.acceleration)), payload.acceleration);
        else if constexpr (std::is_same_v<T, kb::scene::ParticleDragModule>)
            AddSlider(add(std::string{ ParticleText("particle.property.coefficient", "Coefficient") }, ScalarText(payload.coefficient)), payload.coefficient, 0.0F, 12.0F);
        else if constexpr (std::is_same_v<T, kb::scene::ParticleColorOverLifeModule>)
            AddGradient(add(std::string{ ParticleText("particle.property.gradient", "Gradient") }, GradientText(payload.gradient)), payload.gradient);
        else if constexpr (std::is_same_v<T, kb::scene::ParticleSizeOverLifeModule>)
            AddCurve(add(std::string{ ParticleText("particle.property.curve", "Curve") }, CurveText(payload.curve)), payload.curve);
        else if constexpr (std::is_same_v<T, kb::scene::ParticleAlphaOverLifeModule>)
            AddCurve(add(std::string{ ParticleText("particle.property.curve", "Curve") }, CurveText(payload.curve)), payload.curve);
        else if constexpr (std::is_same_v<T, kb::scene::ParticleCollisionPlaneModule>) {
            AddVector(add(std::string{ ParticleText("particle.property.normal", "Normal") }, VecText(payload.normal)), payload.normal);
            AddSlider(add(std::string{ ParticleText("particle.property.distance", "Distance") }, ScalarText(payload.distance)), payload.distance, -20.0F, 20.0F);
            AddSlider(add(std::string{ ParticleText("particle.property.restitution", "Restitution") }, ScalarText(payload.restitution)), payload.restitution, 0.0F, 1.0F);
            AddSlider(add(std::string{ ParticleText("particle.property.friction", "Friction") }, ScalarText(payload.friction)), payload.friction, 0.0F, 1.0F);
            AddIntegerSlider(add(std::string{ ParticleText("particle.property.max_events", "Max events") }, ScalarText(payload.maxEventsPerStep)), payload.maxEventsPerStep, 1U, 256U);
        } else if constexpr (std::is_same_v<T, kb::scene::ParticleSubEmitterModule>) {
            add(std::string{ ParticleText("particle.property.target_emitter", "Target emitter") }, ScalarText(payload.targetEmitterId));
            AddEnum(add(std::string{ ParticleText("particle.property.trigger", "Trigger") }, ScalarText(static_cast<std::uint32_t>(payload.trigger))),
                static_cast<std::uint32_t>(payload.trigger), {ParticleText("particle.choice.birth", "Birth"), ParticleText("particle.choice.death", "Death"), ParticleText("particle.choice.collision", "Collision")});
            AddIntegerSlider(add(std::string{ ParticleText("particle.property.count", "Count") }, ScalarText(payload.count)), payload.count, 1U, 16U);
            AddIntegerSlider(add(std::string{ ParticleText("particle.property.max_depth", "Max depth") }, ScalarText(payload.maxDepth)), payload.maxDepth, 1U, 3U);
        } else if constexpr (std::is_same_v<T, kb::scene::ParticleNoiseModule>) {
            AddSlider(add(std::string{ ParticleText("particle.property.strength", "Strength") }, ScalarText(payload.strength)), payload.strength, 0.0F, 20.0F);
            AddSlider(add(std::string{ ParticleText("particle.property.frequency", "Frequency") }, ScalarText(payload.frequency)), payload.frequency, 0.0F, 10.0F);
        } else if constexpr (std::is_same_v<T, kb::scene::ParticleAxisRotationModule>) {
            AddVector(add(std::string{ ParticleText("particle.property.axis", "Axis") }, VecText(payload.axis)), payload.axis);
            AddSlider(add(std::string{ ParticleText("particle.property.degrees_per_second", "Degrees per second") }, ScalarText(payload.degreesPerSecond)), payload.degreesPerSecond, -720.0F, 720.0F);
        } else if constexpr (std::is_same_v<T, kb::scene::ParticleAttractionModule>) {
            AddVector(add(std::string{ ParticleText("particle.property.point", "Point") }, VecText(payload.point)), payload.point);
            AddSlider(add(std::string{ ParticleText("particle.property.strength", "Strength") }, ScalarText(payload.strength)), payload.strength, -50.0F, 50.0F);
            AddSlider(add(std::string{ ParticleText("particle.property.radius", "Radius") }, ScalarText(payload.radius)), payload.radius, 0.0F, 50.0F);
        } else if constexpr (std::is_same_v<T, kb::scene::ParticleTorusSwirlModule>) {
            AddVector(add(std::string{ ParticleText("particle.property.center", "Center") }, VecText(payload.center)), payload.center);
            AddSlider(add(std::string{ ParticleText("particle.property.radius", "Radius") }, ScalarText(payload.radius)), payload.radius, 0.0F, 50.0F);
            AddSlider(add(std::string{ ParticleText("particle.property.degrees_per_second", "Degrees per second") }, ScalarText(payload.degreesPerSecond)), payload.degreesPerSecond, -720.0F, 720.0F);
        } else if constexpr (std::is_same_v<T, kb::scene::ParticleSeparationModule>) {
            AddSlider(add(std::string{ ParticleText("particle.property.strength", "Strength") }, ScalarText(payload.strength)), payload.strength, 0.0F, 50.0F);
            AddSlider(add(std::string{ ParticleText("particle.property.distance", "Distance") }, ScalarText(payload.distance)), payload.distance, 0.01F, 10.0F);
        }
    }, module.payload);
}

} // namespace

std::string ParticleEmitterInspectorModel::FormatColor(const kb::math::Color& color) {
    return FloatText(color.r) + ' ' + FloatText(color.g) + ' ' + FloatText(color.b) + ' ' + FloatText(color.a);
}

std::string ParticleEmitterInspectorModel::FormatGradient(const kb::math::Gradient& gradient) {
    return GradientText(gradient);
}

namespace {

[[nodiscard]] bool ParseFloatToken(std::string_view text, float& value) noexcept {
    if (text.empty()) return false;
    double parsed = 0.0;
    if (!kb::library::TryParseDouble(text, parsed) || !std::isfinite(parsed) ||
        std::abs(parsed) > std::numeric_limits<float>::max())
        return false;
    value = static_cast<float>(parsed);
    return true;
}

[[nodiscard]] bool ParseUIntToken(std::string_view text, std::uint32_t& value) noexcept {
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

[[nodiscard]] bool NextWhitespaceToken(std::string_view& text, std::string_view& token) noexcept {
    const auto isSpace = [](char ch) noexcept {
        return ch == ' ' || ch == '\t';
    };
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    if (text.empty()) return false;
    std::size_t end = 0U;
    while (end < text.size() && !isSpace(text[end])) ++end;
    token = text.substr(0U, end);
    text.remove_prefix(end);
    return true;
}

[[nodiscard]] bool SplitDelimited(
    std::string_view text, char separator, std::string_view* tokens, std::size_t count) noexcept {
    std::size_t start = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        if (index + 1U == count) {
            tokens[index] = text.substr(start);
            return !tokens[index].empty();
        }
        const std::size_t found = text.find(separator, start);
        if (found == std::string_view::npos) return false;
        tokens[index] = text.substr(start, found - start);
        if (tokens[index].empty()) return false;
        start = found + 1U;
    }
    return false;
}

} // namespace

bool ParticleEmitterInspectorModel::ParseColor(std::string_view text, kb::math::Color& value) noexcept {
    std::string_view remaining = text;
    std::string_view tokens[4]{};
    for (std::string_view& token : tokens) {
        if (!NextWhitespaceToken(remaining, token)) return false;
    }
    std::string_view extra;
    if (NextWhitespaceToken(remaining, extra)) return false;
    kb::math::Color parsed{};
    if (!ParseFloatToken(tokens[0], parsed.r) || !ParseFloatToken(tokens[1], parsed.g) ||
        !ParseFloatToken(tokens[2], parsed.b) || !ParseFloatToken(tokens[3], parsed.a))
        return false;
    value = parsed;
    return true;
}

bool ParticleEmitterInspectorModel::ParseVec3(std::string_view text, kb::math::Vec3& value) noexcept {
    std::string_view remaining = text;
    std::string_view tokens[3]{};
    for (std::string_view& token : tokens) {
        if (!NextWhitespaceToken(remaining, token)) return false;
    }
    std::string_view extra;
    if (NextWhitespaceToken(remaining, extra)) return false;
    kb::math::Vec3 parsed{};
    if (!ParseFloatToken(tokens[0], parsed.x) || !ParseFloatToken(tokens[1], parsed.y) ||
        !ParseFloatToken(tokens[2], parsed.z))
        return false;
    value = parsed;
    return true;
}

bool ParticleEmitterInspectorModel::ParseCurve(std::string_view text, kb::math::Curve& value) {
    if (text.size() > 1U && text.back() == ';') text.remove_suffix(1U);
    std::vector<kb::math::CurveKeyframe> keyframes;
    std::size_t start = 0U;
    while (true) {
        if (keyframes.size() >= kb::scene::kParticleEffectMaxCurveKeys) return false;
        const std::size_t end = text.find(';', start);
        const std::string_view token =
            text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        std::string_view fields[3]{};
        if (!SplitDelimited(token, ',', fields, 3U)) return false;
        kb::math::CurveKeyframe keyframe;
        std::uint32_t easing = 0U;
        if (!ParseFloatToken(fields[0], keyframe.time) || !ParseFloatToken(fields[1], keyframe.value) ||
            !ParseUIntToken(fields[2], easing) ||
            easing > static_cast<std::uint32_t>(kb::math::Easing::InOutBounce))
            return false;
        keyframe.easing = static_cast<kb::math::Easing>(easing);
        keyframes.push_back(keyframe);
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    if (keyframes.empty()) return false;
    value.keyframes = std::move(keyframes);
    return true;
}

bool ParticleEmitterInspectorModel::ParseGradient(std::string_view text, kb::math::Gradient& value) {
    if (text.size() > 1U && text.back() == ';') text.remove_suffix(1U);
    std::vector<kb::math::GradientStop> stops;
    std::size_t start = 0U;
    while (true) {
        if (stops.size() >= kb::scene::kParticleEffectMaxGradientStops) return false;
        const std::size_t end = text.find(';', start);
        const std::string_view token =
            text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        std::string_view fields[5]{};
        if (!SplitDelimited(token, ',', fields, 5U)) return false;
        kb::math::GradientStop stop;
        if (!ParseFloatToken(fields[0], stop.time) || !ParseFloatToken(fields[1], stop.color.r) ||
            !ParseFloatToken(fields[2], stop.color.g) || !ParseFloatToken(fields[3], stop.color.b) ||
            !ParseFloatToken(fields[4], stop.color.a))
            return false;
        stops.push_back(stop);
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    if (stops.empty()) return false;
    value.stops = std::move(stops);
    return true;
}

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
        view.outputChoices.push_back({.type = type, .label = std::string{ OutputName(type) },
                                      .enabled = diagnostics.empty(), .diagnostics = std::move(diagnostics)});
    }
    const auto selected = std::find_if(asset.emitters.begin(), asset.emitters.end(),
        [selectedEmitterId](const auto& value) { return value.emitterId == selectedEmitterId; });
    if (selected != asset.emitters.end()) {
        const auto& spawn = selected->spawn;
        const auto addProperty = [&](ParticleEditorProperty property, std::string label, std::string value,
                                     bool editable = true) -> ParticleEditorPropertyRow& {
            view.properties.push_back({.property = property, .label = std::move(label),
                                       .value = std::move(value), .editable = editable});
            return view.properties.back();
        };
        AddCurve(addProperty(ParticleEditorProperty::SpawnRateCurve, std::string{ ParticleText("particle.property.rate_curve", "Rate curve") }, CurveText(spawn.rateOverTime)),
            spawn.rateOverTime);
        addProperty(ParticleEditorProperty::SpawnBurstsSummary, std::string{ ParticleText("particle.property.bursts", "Bursts") },
            std::to_string(spawn.bursts.size()) + " entries", false);
        AddSlider(addProperty(ParticleEditorProperty::SpawnLifetimeMin, std::string{ ParticleText("particle.property.lifetime_min", "Lifetime min") }, ScalarText(spawn.lifetimeMin)),
            spawn.lifetimeMin, 0.05F, 10.0F);
        AddSlider(addProperty(ParticleEditorProperty::SpawnLifetimeMax, std::string{ ParticleText("particle.property.lifetime_max", "Lifetime max") }, ScalarText(spawn.lifetimeMax)),
            spawn.lifetimeMax, 0.05F, 10.0F);
        AddColor(addProperty(ParticleEditorProperty::SpawnStartColor, std::string{ ParticleText("particle.property.start_color", "Start color") },
            FormatColor(spawn.startColor)), spawn.startColor);
        AddSlider(addProperty(ParticleEditorProperty::SpawnStartSize, std::string{ ParticleText("particle.property.start_size", "Start size") }, ScalarText(spawn.startSize)),
            spawn.startSize, 0.01F, 8.0F);
        // X, Y, Z in degrees; Z turns a billboard, a mesh is turned by all three (Euler: X, then Y, then Z).
        AddVector(addProperty(ParticleEditorProperty::SpawnRotationMin, std::string{ ParticleText("particle.property.rotation_min_xyz_deg", "Rotation min XYZ (deg)") }, VecText(spawn.initialRotationMinDegrees)),
            spawn.initialRotationMinDegrees);
        AddVector(addProperty(ParticleEditorProperty::SpawnRotationMax, std::string{ ParticleText("particle.property.rotation_max_xyz_deg", "Rotation max XYZ (deg)") }, VecText(spawn.initialRotationMaxDegrees)),
            spawn.initialRotationMaxDegrees);
        AddVector(addProperty(ParticleEditorProperty::SpawnAngularVelocityMin, std::string{ ParticleText("particle.property.spin_min_xyz_deg_s", "Spin min XYZ (deg/s)") }, VecText(spawn.angularVelocityMinDegrees)),
            spawn.angularVelocityMinDegrees);
        AddVector(addProperty(ParticleEditorProperty::SpawnAngularVelocityMax, std::string{ ParticleText("particle.property.spin_max_xyz_deg_s", "Spin max XYZ (deg/s)") }, VecText(spawn.angularVelocityMaxDegrees)),
            spawn.angularVelocityMaxDegrees);
        AddSlider(addProperty(ParticleEditorProperty::SpawnSpeedMin, std::string{ ParticleText("particle.property.speed_min", "Speed min") }, ScalarText(spawn.speedMin)),
            spawn.speedMin, 0.0F, 40.0F);
        AddSlider(addProperty(ParticleEditorProperty::SpawnSpeedMax, std::string{ ParticleText("particle.property.speed_max", "Speed max") }, ScalarText(spawn.speedMax)),
            spawn.speedMax, 0.0F, 40.0F);
        AddVector(addProperty(ParticleEditorProperty::SpawnDirection, std::string{ ParticleText("particle.property.direction", "Direction") }, VecText(spawn.direction)),
            spawn.direction);
        AddSlider(addProperty(ParticleEditorProperty::SpawnSpreadDegrees, std::string{ ParticleText("particle.property.spread", "Spread") }, ScalarText(spawn.spreadDegrees)),
            spawn.spreadDegrees, 0.0F, 180.0F);
        AddSlider(addProperty(ParticleEditorProperty::SpawnRandomization, std::string{ ParticleText("particle.property.randomization", "Randomization") }, ScalarText(spawn.randomization)),
            spawn.randomization, 0.0F, 1.0F);
        AddSlider(addProperty(ParticleEditorProperty::SpawnPrewarmSeconds, std::string{ ParticleText("particle.property.prewarm", "Prewarm") }, ScalarText(spawn.prewarmSeconds)),
            spawn.prewarmSeconds, 0.0F, 8.0F);
        AddEnum(addProperty(ParticleEditorProperty::OutputBlend, std::string{ ParticleText("particle.property.blend", "Blend") },
            ScalarText(static_cast<std::uint32_t>(selected->output.blend))),
            static_cast<std::uint32_t>(selected->output.blend),
            {ParticleText("particle.choice.opaque", "Opaque"), ParticleText("particle.choice.alpha", "Alpha"), ParticleText("particle.choice.add", "Add"), ParticleText("particle.choice.multiply", "Multiply"), ParticleText("particle.choice.subtract", "Subtract"), ParticleText("particle.choice.premul", "Premul")});
        AddEnum(addProperty(ParticleEditorProperty::OutputSort, std::string{ ParticleText("particle.property.sort", "Sort") },
            ScalarText(static_cast<std::uint32_t>(selected->output.sort))),
            static_cast<std::uint32_t>(selected->output.sort),
            {ParticleText("particle.choice.none", "None"), ParticleText("particle.choice.back_to_front", "Back to Front"), ParticleText("particle.choice.front_to_back", "Front to Back"), ParticleText("particle.choice.distance", "Distance"), ParticleText("particle.choice.age", "Age")});
        AddToggle(addProperty(ParticleEditorProperty::OutputDepthTest, std::string{ ParticleText("particle.property.depth_test", "Depth test") },
            selected->output.depthTest ? "true" : "false"), selected->output.depthTest);
        AddToggle(addProperty(ParticleEditorProperty::OutputDepthWrite, std::string{ ParticleText("particle.property.depth_write", "Depth write") },
            selected->output.depthWrite ? "true" : "false"), selected->output.depthWrite);
        AddToggle(addProperty(ParticleEditorProperty::OutputScreenDepthCollision,
            std::string{ParticleText("particle.property.screen_depth_collision", "Screen depth collision (GPU)")},
            selected->screenDepthCollision ? "true" : "false"), selected->screenDepthCollision);
        AddToggle(addProperty(ParticleEditorProperty::OutputSoftParticles, std::string{ ParticleText("particle.property.soft_particles", "Soft particles") },
            selected->output.softParticles ? "true" : "false"), selected->output.softParticles);
        AddToggle(addProperty(ParticleEditorProperty::OutputAntiAliasing, std::string{ ParticleText("particle.property.anti_aliasing", "Anti-aliasing") },
            selected->output.antiAliasing ? "true" : "false"), selected->output.antiAliasing);
        AddEnum(addProperty(ParticleEditorProperty::OutputAlignment, std::string{ ParticleText("particle.property.alignment", "Alignment") },
            ScalarText(static_cast<std::uint32_t>(selected->output.alignment))),
            static_cast<std::uint32_t>(selected->output.alignment),
            {ParticleText("particle.choice.camera", "Camera"), ParticleText("particle.choice.velocity", "Velocity"), ParticleText("particle.choice.world_up", "World Up"), ParticleText("particle.choice.local", "Local")});
        std::visit([&](const auto& payload) {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, kb::scene::ParticleBillboardOutput> ||
                          std::is_same_v<T, kb::scene::ParticleStretchedBillboardOutput> ||
                          std::is_same_v<T, kb::scene::ParticlePointSpriteOutput>) {
                AddIntegerSlider(addProperty(ParticleEditorProperty::FlipbookColumns, std::string{ ParticleText("particle.property.atlas_columns", "Atlas columns") },
                    ScalarText(payload.flipbook.columns)), payload.flipbook.columns, 1U, 16U);
                AddIntegerSlider(addProperty(ParticleEditorProperty::FlipbookRows, std::string{ ParticleText("particle.property.atlas_rows", "Atlas rows") },
                    ScalarText(payload.flipbook.rows)), payload.flipbook.rows, 1U, 16U);
                AddSlider(addProperty(ParticleEditorProperty::FlipbookFramesPerSecond, std::string{ ParticleText("particle.property.frame_rate", "Frame rate") },
                    ScalarText(payload.flipbook.framesPerSecond)), payload.flipbook.framesPerSecond, 0.0F, 60.0F);
                AddToggle(addProperty(ParticleEditorProperty::FlipbookLooping, std::string{ ParticleText("particle.property.flipbook_loop", "Flipbook loop") },
                    payload.flipbook.looping ? "true" : "false"), payload.flipbook.looping);
                if constexpr (std::is_same_v<T, kb::scene::ParticleStretchedBillboardOutput>) {
                    AddSlider(addProperty(ParticleEditorProperty::OutputVelocityScale, std::string{ ParticleText("particle.property.velocity_scale", "Velocity scale") },
                        ScalarText(payload.velocityScale)), payload.velocityScale, 0.0F, 8.0F);
                    AddSlider(addProperty(ParticleEditorProperty::OutputMinimumLength, std::string{ ParticleText("particle.property.minimum_length", "Minimum length") },
                        ScalarText(payload.minimumLength)), payload.minimumLength, 0.0F, 4.0F);
                } else if constexpr (std::is_same_v<T, kb::scene::ParticlePointSpriteOutput>)
                    AddSlider(addProperty(ParticleEditorProperty::OutputPointDiameter, std::string{ ParticleText("particle.property.point_diameter", "Point diameter") },
                        ScalarText(payload.diameter)), payload.diameter, 0.02F, 4.0F);
            }
        }, selected->output.payload);
        view.modules.reserve(selected->modules.size());
        for (const auto& module : selected->modules) {
            view.modules.push_back({.moduleId = module.moduleId, .authoringOrder = module.authoringOrder,
                .type = module.type, .label = std::string{ ModuleName(module.type) }, .summary = ModuleSummary(module),
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
