#include "engine/scene/ParticleEffectAssetIO.hpp"

#include "engine/library/EngineLibraryParsing.hpp"
#include "engine/scene/ParticleEffectAssetMigration.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>

namespace kb::scene {
namespace {

struct Record {
    std::string_view key;
    std::string_view value;
    std::uint32_t line = 0U;
    bool consumed = false;
};
using Records = std::vector<Record>;
inline constexpr std::size_t kParticleEffectMaxRecordStorageBytes = kParticleEffectMaxRecords * sizeof(Record);
static_assert(kParticleEffectMaxRecordStorageBytes / sizeof(Record) == kParticleEffectMaxRecords);

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
        text.remove_prefix(1U);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1U);
    return text;
}

void Error(ParticleEffectLoadResult& result, ParticleEffectDiagnosticCode code, std::uint32_t line, std::string path,
           std::string message) {
    if (result.diagnostics.size() > kParticleEffectMaxDiagnostics)
        return;
    if (result.diagnostics.size() == kParticleEffectMaxDiagnostics) {
        result.diagnostics.push_back(
            ParticleEffectDiagnostic{.code = ParticleEffectDiagnosticCode::LimitExceeded,
                                     .line = line,
                                     .propertyPath = std::move(path),
                                     .message = "diagnostic collection reached its schema hard limit"});
        return;
    }
    result.diagnostics.push_back(ParticleEffectDiagnostic{
        .code = code, .line = line, .propertyPath = std::move(path), .message = std::move(message)});
}

[[nodiscard]] bool ParseFloat(std::string_view text, float& output) noexcept {
    double value = 0.0;
    if (!kb::library::TryParseDouble(Trim(text), value) || !std::isfinite(value) ||
        std::abs(value) > std::numeric_limits<float>::max())
        return false;
    output = static_cast<float>(value);
    return true;
}

template <typename UInt> [[nodiscard]] bool ParseUInt(std::string_view text, UInt& output) noexcept {
    text = Trim(text);
    UInt value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return false;
    output = value;
    return true;
}

[[nodiscard]] bool ParseBool(std::string_view text, bool& output) noexcept {
    text = Trim(text);
    if (text == "true" || text == "1") {
        output = true;
        return true;
    }
    if (text == "false" || text == "0") {
        output = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseQuoted(std::string_view text, std::string& output) {
    text = Trim(text);
    if (text.size() < 2U || text.front() != '"' || text.back() != '"')
        return false;
    output.clear();
    const std::size_t delimiterIndex = text.size() - 1U;
    for (std::size_t index = 1U; index < delimiterIndex; ++index) {
        char value = text[index];
        if (value == '\\') {
            if (index + 1U >= delimiterIndex)
                return false;
            ++index;
            switch (text[index]) {
            case '\\':
                value = '\\';
                break;
            case '"':
                value = '"';
                break;
            case 'n':
                value = '\n';
                break;
            case 'r':
                value = '\r';
                break;
            case 't':
                value = '\t';
                break;
            default:
                return false;
            }
        } else if (value == '"' || static_cast<unsigned char>(value) < 0x20U)
            return false;
        output.push_back(value);
        if (output.size() > kParticleEffectMaxStringBytes)
            return false;
    }
    return IsValidParticleEffectString(output);
}

[[nodiscard]] std::vector<std::string_view> Tokens(std::string_view text) {
    std::vector<std::string_view> tokens;
    text = Trim(text);
    while (!text.empty()) {
        const std::size_t split = text.find_first_of(" \t");
        tokens.push_back(text.substr(0U, split));
        if (split == std::string_view::npos)
            break;
        text = Trim(text.substr(split));
    }
    return tokens;
}

[[nodiscard]] bool ParseVec3(std::string_view text, kb::math::Vec3& value) {
    const auto tokens = Tokens(text);
    return tokens.size() == 3U && ParseFloat(tokens[0], value.x) && ParseFloat(tokens[1], value.y) &&
           ParseFloat(tokens[2], value.z);
}

[[nodiscard]] bool ParseQuat(std::string_view text, kb::math::Quat& value) {
    const auto tokens = Tokens(text);
    return tokens.size() == 4U && ParseFloat(tokens[0], value.x) && ParseFloat(tokens[1], value.y) &&
           ParseFloat(tokens[2], value.z) && ParseFloat(tokens[3], value.w);
}

template <typename Enum> struct EnumName;

#define KB_PARTICLE_ENUM_NAMES(Type, ...)                                                                              \
    template <> struct EnumName<Type> {                                                                                \
        static constexpr std::pair<Type, std::string_view> Values[] = {__VA_ARGS__};                                   \
    }
KB_PARTICLE_ENUM_NAMES(ParticleBackendPolicy, {ParticleBackendPolicy::CpuDeterministic, "CpuDeterministic"},
                       {ParticleBackendPolicy::GpuVisualPreferred, "GpuVisualPreferred"},
                       {ParticleBackendPolicy::GpuVisualRequired, "GpuVisualRequired"});
KB_PARTICLE_ENUM_NAMES(ParticleGpuCatchupPolicy, {ParticleGpuCatchupPolicy::RestartFromSeed, "RestartFromSeed"},
                       {ParticleGpuCatchupPolicy::BoundedWarmup, "BoundedWarmup"});
KB_PARTICLE_ENUM_NAMES(ParticleSimulationSpace, {ParticleSimulationSpace::Local, "Local"},
                       {ParticleSimulationSpace::World, "World"});
KB_PARTICLE_ENUM_NAMES(ParticleSpawnMode, {ParticleSpawnMode::Continuous, "Continuous"},
                       {ParticleSpawnMode::Burst, "Burst"});
KB_PARTICLE_ENUM_NAMES(ParticleModuleType, {ParticleModuleType::InitialVelocity, "InitialVelocity"},
                       {ParticleModuleType::Gravity, "Gravity"}, {ParticleModuleType::Wind, "Wind"},
                       {ParticleModuleType::Drag, "Drag"}, {ParticleModuleType::ColorOverLife, "ColorOverLife"},
                       {ParticleModuleType::SizeOverLife, "SizeOverLife"},
                       {ParticleModuleType::AlphaOverLife, "AlphaOverLife"},
                       {ParticleModuleType::CollisionPlane, "CollisionPlane"},
                       {ParticleModuleType::SubEmitter, "SubEmitter"});
KB_PARTICLE_ENUM_NAMES(ParticleEventTrigger, {ParticleEventTrigger::Birth, "Birth"},
                       {ParticleEventTrigger::Death, "Death"}, {ParticleEventTrigger::Collision, "Collision"});
KB_PARTICLE_ENUM_NAMES(ParticleEventAction, {ParticleEventAction::EmitTargetEmitter, "EmitTargetEmitter"},
                       {ParticleEventAction::EmitEffectAsset, "EmitEffectAsset"});
KB_PARTICLE_ENUM_NAMES(ParticleOutputType, {ParticleOutputType::Billboard, "Billboard"},
                       {ParticleOutputType::StretchedBillboard, "StretchedBillboard"},
                       {ParticleOutputType::PointSprite, "PointSprite"}, {ParticleOutputType::Mesh, "Mesh"},
                       {ParticleOutputType::Trail, "Trail"}, {ParticleOutputType::Ribbon, "Ribbon"},
                       {ParticleOutputType::Beam, "Beam"}, {ParticleOutputType::Volumetric, "Volumetric"});
KB_PARTICLE_ENUM_NAMES(ParticleBlendMode, {ParticleBlendMode::Opaque, "Opaque"}, {ParticleBlendMode::Alpha, "Alpha"},
                       {ParticleBlendMode::Add, "Add"}, {ParticleBlendMode::Multiply, "Multiply"},
                       {ParticleBlendMode::Subtract, "Subtract"}, {ParticleBlendMode::Premultiplied, "Premultiplied"});
KB_PARTICLE_ENUM_NAMES(ParticleSortMode, {ParticleSortMode::None, "None"},
                       {ParticleSortMode::BackToFront, "BackToFront"}, {ParticleSortMode::FrontToBack, "FrontToBack"},
                       {ParticleSortMode::Distance, "Distance"}, {ParticleSortMode::Age, "Age"});
KB_PARTICLE_ENUM_NAMES(ParticleAlignment, {ParticleAlignment::CameraFacing, "CameraFacing"},
                       {ParticleAlignment::Velocity, "Velocity"}, {ParticleAlignment::WorldUp, "WorldUp"},
                       {ParticleAlignment::Local, "Local"});
#undef KB_PARTICLE_ENUM_NAMES

template <typename Enum> [[nodiscard]] bool ParseEnum(std::string_view text, Enum& output) noexcept {
    text = Trim(text);
    for (const auto& [value, name] : EnumName<Enum>::Values)
        if (text == name) {
            output = value;
            return true;
        }
    return false;
}

template <typename Enum> [[nodiscard]] std::string_view EnumText(Enum value) noexcept {
    for (const auto& [candidate, name] : EnumName<Enum>::Values)
        if (candidate == value)
            return name;
    return {};
}

[[nodiscard]] bool ParseEasing(std::string_view text, kb::math::Easing& output) noexcept {
    text = Trim(text);
    for (std::uint32_t value = 0U; value <= static_cast<std::uint32_t>(kb::math::Easing::InOutBounce); ++value) {
        const auto candidate = static_cast<kb::math::Easing>(value);
        if (kb::math::ToString(candidate) == text) {
            output = candidate;
            return true;
        }
    }
    return false;
}

class Reader {
  public:
    Reader(Records records, ParticleEffectLoadResult& result, std::uint32_t headerLine)
        : records_(std::move(records)), result_(result), headerLine_(headerLine) {}

    template <typename T, typename Parse>
    void Required(const std::string& key, T& output, Parse parse,
                  ParticleEffectDiagnosticCode code = ParticleEffectDiagnosticCode::InvalidValue) {
        auto it = Find(key);
        if (it == records_.end()) {
            Error(result_, ParticleEffectDiagnosticCode::MissingKey, ProvenanceLine(key), key,
                  "required property is missing");
            return;
        }
        if (!parse(it->value, output))
            Error(result_, code, it->line, key, "property value is invalid");
        it->consumed = true;
    }

    template <typename T, typename Parse>
    bool Optional(const std::string& key, T& output, Parse parse,
                  ParticleEffectDiagnosticCode code = ParticleEffectDiagnosticCode::InvalidValue) {
        auto it = Find(key);
        if (it == records_.end())
            return false;
        if (!parse(it->value, output))
            Error(result_, code, it->line, key, "property value is invalid");
        it->consumed = true;
        return true;
    }

    template <typename T, typename Parse>
    void RequiredIndexAnchor(const std::string& key, T& output, Parse parse,
                             ParticleEffectDiagnosticCode code = ParticleEffectDiagnosticCode::InvalidValue) {
        auto it = Find(key);
        if (it == records_.end()) {
            Error(result_, ParticleEffectDiagnosticCode::CountMismatch, ProvenanceLine(key), key,
                  "declared count has no corresponding indexed record");
            return;
        }
        if (!parse(it->value, output))
            Error(result_, code, it->line, key, "property value is invalid");
        it->consumed = true;
    }

    void RequiredBoundedCount(const std::string& key, std::uint32_t& output, std::size_t maximum) {
        auto it = Find(key);
        if (it == records_.end()) {
            Error(result_, ParticleEffectDiagnosticCode::MissingKey, ProvenanceLine(key), key,
                  "required count is missing");
            return;
        }
        if (!ParseUInt(it->value, output))
            Error(result_, ParticleEffectDiagnosticCode::InvalidValue, it->line, key, "count is invalid");
        else if (output > maximum)
            Error(result_, ParticleEffectDiagnosticCode::LimitExceeded, it->line, key,
                  "count exceeds the schema hard limit");
        it->consumed = true;
    }

    void Finish() {
        for (const Record& record : records_)
            if (!record.consumed) {
                Error(result_, ParticleEffectDiagnosticCode::UnknownKey, record.line, std::string{record.key},
                      "property is not recognized by schema version 2");
                break;
            }
    }

    [[nodiscard]] std::uint32_t ProvenanceLine(std::string_view key) const noexcept {
        if (const auto exact = Find(key); exact != records_.end())
            return exact->line;
        const auto lineFor = [&](std::string_view candidate) -> std::uint32_t {
            const auto found = Find(candidate);
            return found == records_.end() ? 0U : found->line;
        };
        const auto indexedAnchor = [&](std::string_view marker, std::string_view suffix) -> std::uint32_t {
            const std::size_t markerPosition = key.find(marker);
            if (markerPosition == std::string_view::npos)
                return 0U;
            const std::size_t close = key.find(']', markerPosition + marker.size());
            if (close == std::string_view::npos)
                return 0U;
            return lineFor(std::string{key.substr(0U, close + 1U)} + std::string{suffix});
        };
        if (const std::uint32_t line = indexedAnchor(".module[", ".id"); line != 0U)
            return line;
        if (const std::uint32_t line = indexedAnchor(".eventBinding[", ".sourceEmitterId"); line != 0U)
            return line;
        for (const auto& [marker, countSuffix] : {std::pair<std::string_view, std::string_view>{".key[", ".keyCount"},
                                                  {".stop[", ".stopCount"},
                                                  {".burst[", ".burstCount"}}) {
            const std::size_t markerPosition = key.find(marker);
            if (markerPosition != std::string_view::npos) {
                if (const std::uint32_t line =
                        lineFor(std::string{key.substr(0U, markerPosition)} + std::string{countSuffix});
                    line != 0U)
                    return line;
            }
        }
        if (const std::size_t module = key.find(".module["); module != std::string_view::npos) {
            if (const std::uint32_t line = lineFor(std::string{key.substr(0U, module)} + ".moduleCount"); line != 0U)
                return line;
        }
        if (key.find(".eventBinding[") != std::string_view::npos) {
            if (const std::uint32_t line = lineFor("effect.eventBindingCount"); line != 0U)
                return line;
        }
        if (const std::uint32_t line = indexedAnchor(".emitter[", ".id"); line != 0U)
            return line;
        if (key.find(".emitter[") != std::string_view::npos) {
            if (const std::uint32_t line = lineFor("effect.emitterCount"); line != 0U)
                return line;
        }
        return headerLine_;
    }

  private:
    [[nodiscard]] Records::iterator Find(std::string_view key) noexcept {
        const auto found =
            std::lower_bound(records_.begin(), records_.end(), key,
                             [](const Record& record, std::string_view candidate) { return record.key < candidate; });
        return found != records_.end() && found->key == key ? found : records_.end();
    }
    [[nodiscard]] Records::const_iterator Find(std::string_view key) const noexcept {
        const auto found =
            std::lower_bound(records_.begin(), records_.end(), key,
                             [](const Record& record, std::string_view candidate) { return record.key < candidate; });
        return found != records_.end() && found->key == key ? found : records_.end();
    }

    Records records_;
    ParticleEffectLoadResult& result_;
    std::uint32_t headerLine_ = 0U;
};

[[nodiscard]] std::string IndexPath(std::size_t emitter, std::string_view suffix) {
    return "effect.emitter[" + std::to_string(emitter) + "]." + std::string{suffix};
}
[[nodiscard]] std::string ModulePath(std::size_t emitter, std::size_t module, std::string_view suffix) {
    return "effect.emitter[" + std::to_string(emitter) + "].module[" + std::to_string(module) + "]." +
           std::string{suffix};
}
[[nodiscard]] std::string EventPath(std::size_t event, std::string_view suffix) {
    return "effect.eventBinding[" + std::to_string(event) + "]." + std::string{suffix};
}

void ReadReference(Reader& reader, const std::string& base, ParticleAssetReference& reference) {
    reader.Required(base + ".assetId", reference.assetId, ParseUInt<std::uint64_t>);
    reader.Required(base + ".path", reference.virtualPath, ParseQuoted, ParticleEffectDiagnosticCode::InvalidEscape);
}

void ReadCurve(Reader& reader, const std::string& base, kb::math::Curve& curve) {
    std::uint32_t count = 0U;
    reader.RequiredBoundedCount(base + ".keyCount", count, kParticleEffectMaxCurveKeys);
    if (count > kParticleEffectMaxCurveKeys)
        return;
    curve.keyframes.resize(count);
    for (std::uint32_t index = 0U; index < count; ++index) {
        const std::string path = base + ".key[" + std::to_string(index) + "]";
        reader.RequiredIndexAnchor(path + ".time", curve.keyframes[index].time, ParseFloat);
        reader.Required(path + ".value", curve.keyframes[index].value, ParseFloat);
        reader.Required(path + ".easing", curve.keyframes[index].easing, ParseEasing,
                        ParticleEffectDiagnosticCode::InvalidEnum);
    }
}

void ReadGradient(Reader& reader, const std::string& base, kb::math::Gradient& gradient) {
    std::uint32_t count = 0U;
    reader.RequiredBoundedCount(base + ".stopCount", count, kParticleEffectMaxGradientStops);
    if (count > kParticleEffectMaxGradientStops)
        return;
    gradient.stops.resize(count);
    for (std::uint32_t index = 0U; index < count; ++index) {
        const std::string path = base + ".stop[" + std::to_string(index) + "]";
        reader.RequiredIndexAnchor(path + ".time", gradient.stops[index].time, ParseFloat);
        kb::math::Vec3 rgb{};
        reader.Required(path + ".rgb", rgb, ParseVec3);
        gradient.stops[index].color.r = rgb.x;
        gradient.stops[index].color.g = rgb.y;
        gradient.stops[index].color.b = rgb.z;
        reader.Required(path + ".alpha", gradient.stops[index].color.a, ParseFloat);
    }
}

void ReadFlipbook(Reader& reader, const std::string& base, ParticleFlipbookAsset& flipbook) {
    reader.Required(base + ".columns", flipbook.columns, ParseUInt<std::uint32_t>);
    reader.Required(base + ".rows", flipbook.rows, ParseUInt<std::uint32_t>);
    reader.Required(base + ".framesPerSecond", flipbook.framesPerSecond, ParseFloat);
    reader.Required(base + ".looping", flipbook.looping, ParseBool);
}

[[nodiscard]] std::optional<std::size_t> ParsePathIndex(std::string_view path, std::string_view marker) noexcept {
    const std::size_t begin = path.find(marker);
    if (begin == std::string_view::npos)
        return std::nullopt;
    const std::size_t valueBegin = begin + marker.size();
    const std::size_t end = path.find(']', valueBegin);
    if (end == std::string_view::npos)
        return std::nullopt;
    std::size_t value = 0U;
    const auto parsed = std::from_chars(path.data() + valueBegin, path.data() + end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != path.data() + end)
        return std::nullopt;
    return value;
}

void AnnotateDiagnosticContext(ParticleEffectLoadResult& result, const ParticleEffectAsset& asset) {
    for (ParticleEffectDiagnostic& diagnostic : result.diagnostics) {
        if (const auto eventIndex = ParsePathIndex(diagnostic.propertyPath, ".eventBinding[");
            eventIndex && *eventIndex < asset.eventBindings.size()) {
            diagnostic.emitterId = asset.eventBindings[*eventIndex].sourceEmitterId;
            diagnostic.moduleId = asset.eventBindings[*eventIndex].sourceModuleId;
            continue;
        }
        const auto emitterIndex = ParsePathIndex(diagnostic.propertyPath, ".emitter[");
        if (!emitterIndex || *emitterIndex >= asset.emitters.size())
            continue;
        const ParticleEmitterAsset& emitter = asset.emitters[*emitterIndex];
        diagnostic.emitterId = emitter.emitterId;
        if (const auto moduleIndex = ParsePathIndex(diagnostic.propertyPath, ".module[");
            moduleIndex && *moduleIndex < emitter.modules.size())
            diagnostic.moduleId = emitter.modules[*moduleIndex].moduleId;
    }
}

[[nodiscard]] ParticleEffectLoadResult ParseV2(Records records, std::uint32_t headerLine) {
    ParticleEffectLoadResult result{};
    result.asset.emplace();
    ParticleEffectAsset& asset = *result.asset;
    Reader reader{std::move(records), result, headerLine};
    reader.Required("effect.formatVersion", asset.formatVersion, ParseUInt<std::uint32_t>);
    reader.Required("effect.id", asset.effectId, ParseUInt<std::uint64_t>);
    reader.Required("effect.displayName", asset.displayName, ParseQuoted, ParticleEffectDiagnosticCode::InvalidEscape);
    reader.Required("effect.recipeCategory", asset.recipeCategory, ParseQuoted,
                    ParticleEffectDiagnosticCode::InvalidEscape);
    reader.Required("effect.determinismSeed", asset.determinismSeed, ParseUInt<std::uint64_t>);
    reader.Required("effect.durationSeconds", asset.durationSeconds, ParseFloat);
    reader.Required("effect.looping", asset.looping, ParseBool);
    reader.Required("effect.backendPolicy", asset.backendPolicy, ParseEnum<ParticleBackendPolicy>,
                    ParticleEffectDiagnosticCode::InvalidEnum);
    reader.Required("effect.gpuCatchupPolicy", asset.gpuCatchupPolicy, ParseEnum<ParticleGpuCatchupPolicy>,
                    ParticleEffectDiagnosticCode::InvalidEnum);
    std::uint32_t emitterCount = 0U;
    reader.RequiredBoundedCount("effect.emitterCount", emitterCount, kParticleEffectMaxEmitters);
    if (emitterCount <= kParticleEffectMaxEmitters)
        asset.emitters.resize(emitterCount);
    for (std::size_t emitterIndex = 0U; emitterIndex < asset.emitters.size(); ++emitterIndex) {
        ParticleEmitterAsset& emitter = asset.emitters[emitterIndex];
        reader.RequiredIndexAnchor(IndexPath(emitterIndex, "id"), emitter.emitterId, ParseUInt<std::uint64_t>);
        emitter.authoringOrder = static_cast<std::uint32_t>(emitterIndex);
        static_cast<void>(reader.Optional(IndexPath(emitterIndex, "authoringOrder"), emitter.authoringOrder,
                                          ParseUInt<std::uint32_t>));
        reader.Required(IndexPath(emitterIndex, "name"), emitter.name, ParseQuoted,
                        ParticleEffectDiagnosticCode::InvalidEscape);
        reader.Required(IndexPath(emitterIndex, "enabled"), emitter.enabled, ParseBool);
        reader.Required(IndexPath(emitterIndex, "localPosition"), emitter.localPosition, ParseVec3);
        reader.Required(IndexPath(emitterIndex, "localRotation"), emitter.localRotation, ParseQuat);
        reader.Required(IndexPath(emitterIndex, "localScale"), emitter.localScale, ParseVec3);
        reader.Required(IndexPath(emitterIndex, "maxParticles"), emitter.maxParticles, ParseUInt<std::uint32_t>);
        reader.Required(IndexPath(emitterIndex, "simulationSpace"), emitter.simulationSpace,
                        ParseEnum<ParticleSimulationSpace>, ParticleEffectDiagnosticCode::InvalidEnum);
        reader.Required(IndexPath(emitterIndex, "spawn.mode"), emitter.spawn.mode, ParseEnum<ParticleSpawnMode>,
                        ParticleEffectDiagnosticCode::InvalidEnum);
        ReadCurve(reader, IndexPath(emitterIndex, "spawn.rate"), emitter.spawn.rateOverTime);
        std::uint32_t burstCount = 0U;
        reader.RequiredBoundedCount(IndexPath(emitterIndex, "spawn.burstCount"), burstCount, kParticleEffectMaxBursts);
        if (burstCount <= kParticleEffectMaxBursts)
            emitter.spawn.bursts.resize(burstCount);
        for (std::size_t burst = 0U; burst < emitter.spawn.bursts.size(); ++burst) {
            const std::string base = IndexPath(emitterIndex, "spawn.burst[" + std::to_string(burst) + "]");
            reader.RequiredIndexAnchor(base + ".timeSeconds", emitter.spawn.bursts[burst].timeSeconds, ParseFloat);
            reader.Required(base + ".count", emitter.spawn.bursts[burst].count, ParseUInt<std::uint32_t>);
        }
        reader.Required(IndexPath(emitterIndex, "spawn.lifetimeMin"), emitter.spawn.lifetimeMin, ParseFloat);
        reader.Required(IndexPath(emitterIndex, "spawn.lifetimeMax"), emitter.spawn.lifetimeMax, ParseFloat);
        reader.Required(IndexPath(emitterIndex, "spawn.speedMin"), emitter.spawn.speedMin, ParseFloat);
        reader.Required(IndexPath(emitterIndex, "spawn.speedMax"), emitter.spawn.speedMax, ParseFloat);
        reader.Required(IndexPath(emitterIndex, "spawn.direction"), emitter.spawn.direction, ParseVec3);
        reader.Required(IndexPath(emitterIndex, "spawn.spreadDegrees"), emitter.spawn.spreadDegrees, ParseFloat);
        reader.Required(IndexPath(emitterIndex, "spawn.randomization"), emitter.spawn.randomization, ParseFloat);
        reader.Required(IndexPath(emitterIndex, "spawn.prewarmSeconds"), emitter.spawn.prewarmSeconds, ParseFloat);
        std::uint32_t moduleCount = 0U;
        reader.RequiredBoundedCount(IndexPath(emitterIndex, "moduleCount"), moduleCount,
                                    kParticleEffectMaxModulesPerEmitter);
        if (moduleCount <= kParticleEffectMaxModulesPerEmitter)
            emitter.modules.resize(moduleCount);
        for (std::size_t moduleIndex = 0U; moduleIndex < emitter.modules.size(); ++moduleIndex) {
            ParticleModuleAsset& module = emitter.modules[moduleIndex];
            reader.RequiredIndexAnchor(ModulePath(emitterIndex, moduleIndex, "id"), module.moduleId,
                                       ParseUInt<std::uint64_t>);
            reader.Required(ModulePath(emitterIndex, moduleIndex, "type"), module.type, ParseEnum<ParticleModuleType>,
                            ParticleEffectDiagnosticCode::InvalidEnum);
            reader.Required(ModulePath(emitterIndex, moduleIndex, "enabled"), module.enabled, ParseBool);
            module.payload = DefaultParticleModulePayload(module.type);
            const std::string base = ModulePath(emitterIndex, moduleIndex, "payload");
            std::visit(
                [&](auto& payload) {
                    using T = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<T, ParticleInitialVelocityModule>) {
                        reader.Required(base + ".direction", payload.direction, ParseVec3);
                        reader.Required(base + ".speedMin", payload.speedMin, ParseFloat);
                        reader.Required(base + ".speedMax", payload.speedMax, ParseFloat);
                        reader.Required(base + ".randomization", payload.randomization, ParseFloat);
                        reader.Required(base + ".spreadDegrees", payload.spreadDegrees, ParseFloat);
                    } else if constexpr (std::is_same_v<T, ParticleGravityModule>) {
                        reader.Required(base + ".acceleration", payload.acceleration, ParseVec3);
                        reader.Required(base + ".sceneGravityScale", payload.sceneGravityScale, ParseFloat);
                    } else if constexpr (std::is_same_v<T, ParticleWindModule>)
                        reader.Required(base + ".acceleration", payload.acceleration, ParseVec3);
                    else if constexpr (std::is_same_v<T, ParticleDragModule>)
                        reader.Required(base + ".coefficient", payload.coefficient, ParseFloat);
                    else if constexpr (std::is_same_v<T, ParticleColorOverLifeModule>)
                        ReadGradient(reader, base + ".gradient", payload.gradient);
                    else if constexpr (std::is_same_v<T, ParticleSizeOverLifeModule> ||
                                       std::is_same_v<T, ParticleAlphaOverLifeModule>)
                        ReadCurve(reader, base + ".curve", payload.curve);
                    else if constexpr (std::is_same_v<T, ParticleCollisionPlaneModule>) {
                        reader.Required(base + ".normal", payload.normal, ParseVec3);
                        reader.Required(base + ".distance", payload.distance, ParseFloat);
                        reader.Required(base + ".restitution", payload.restitution, ParseFloat);
                        reader.Required(base + ".friction", payload.friction, ParseFloat);
                        reader.Required(base + ".maxEventsPerStep", payload.maxEventsPerStep, ParseUInt<std::uint32_t>);
                    } else if constexpr (std::is_same_v<T, ParticleSubEmitterModule>) {
                        reader.Required(base + ".targetEmitterId", payload.targetEmitterId, ParseUInt<std::uint64_t>);
                        reader.Required(base + ".trigger", payload.trigger, ParseEnum<ParticleEventTrigger>,
                                        ParticleEffectDiagnosticCode::InvalidEnum);
                        reader.Required(base + ".count", payload.count, ParseUInt<std::uint32_t>);
                        reader.Required(base + ".maxDepth", payload.maxDepth, ParseUInt<std::uint32_t>);
                    }
                },
                module.payload);
        }
        ParticleOutputAsset& output = emitter.output;
        reader.Required(IndexPath(emitterIndex, "output.type"), output.type, ParseEnum<ParticleOutputType>,
                        ParticleEffectDiagnosticCode::InvalidEnum);
        ReadReference(reader, IndexPath(emitterIndex, "output.material"), output.material);
        ReadReference(reader, IndexPath(emitterIndex, "output.mesh"), output.mesh);
        ReadReference(reader, IndexPath(emitterIndex, "output.textureAtlas"), output.textureAtlas);
        reader.Required(IndexPath(emitterIndex, "output.blend"), output.blend, ParseEnum<ParticleBlendMode>,
                        ParticleEffectDiagnosticCode::InvalidEnum);
        reader.Required(IndexPath(emitterIndex, "output.sort"), output.sort, ParseEnum<ParticleSortMode>,
                        ParticleEffectDiagnosticCode::InvalidEnum);
        reader.Required(IndexPath(emitterIndex, "output.depthTest"), output.depthTest, ParseBool);
        reader.Required(IndexPath(emitterIndex, "output.depthWrite"), output.depthWrite, ParseBool);
        reader.Required(IndexPath(emitterIndex, "output.softParticles"), output.softParticles, ParseBool);
        reader.Required(IndexPath(emitterIndex, "output.antiAliasing"), output.antiAliasing, ParseBool);
        reader.Required(IndexPath(emitterIndex, "output.alignment"), output.alignment, ParseEnum<ParticleAlignment>,
                        ParticleEffectDiagnosticCode::InvalidEnum);
        output.payload = DefaultParticleOutputPayload(output.type);
        const std::string base = IndexPath(emitterIndex, "output.payload");
        std::visit(
            [&](auto& payload) {
                using T = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<T, ParticleBillboardOutput>)
                    ReadFlipbook(reader, base + ".flipbook", payload.flipbook);
                else if constexpr (std::is_same_v<T, ParticleStretchedBillboardOutput>) {
                    ReadFlipbook(reader, base + ".flipbook", payload.flipbook);
                    reader.Required(base + ".velocityScale", payload.velocityScale, ParseFloat);
                    reader.Required(base + ".minimumLength", payload.minimumLength, ParseFloat);
                } else if constexpr (std::is_same_v<T, ParticlePointSpriteOutput>) {
                    ReadFlipbook(reader, base + ".flipbook", payload.flipbook);
                    reader.Required(base + ".diameter", payload.diameter, ParseFloat);
                } else if constexpr (std::is_same_v<T, ParticleMeshOutput>) {
                    reader.Required(base + ".lodBias", payload.lodBias, ParseFloat);
                    reader.Required(base + ".castsShadow", payload.castsShadow, ParseBool);
                    reader.Required(base + ".receivesShadow", payload.receivesShadow, ParseBool);
                } else if constexpr (std::is_same_v<T, ParticleTrailOutput>) {
                    reader.Required(base + ".sampleIntervalSeconds", payload.sampleIntervalSeconds, ParseFloat);
                    reader.Required(base + ".minimumDistance", payload.minimumDistance, ParseFloat);
                    reader.Required(base + ".maxSamplesPerParticle", payload.maxSamplesPerParticle,
                                    ParseUInt<std::uint32_t>);
                    reader.Required(base + ".width", payload.width, ParseFloat);
                } else if constexpr (std::is_same_v<T, ParticleRibbonOutput>) {
                    reader.Required(base + ".maxSegments", payload.maxSegments, ParseUInt<std::uint32_t>);
                    reader.Required(base + ".width", payload.width, ParseFloat);
                    reader.Required(base + ".breakOnDeath", payload.breakOnDeath, ParseBool);
                } else if constexpr (std::is_same_v<T, ParticleBeamOutput>) {
                    reader.Required(base + ".localEnd", payload.localEnd, ParseVec3);
                    reader.Required(base + ".segments", payload.segments, ParseUInt<std::uint32_t>);
                    reader.Required(base + ".width", payload.width, ParseFloat);
                    reader.Required(base + ".noiseAmplitude", payload.noiseAmplitude, ParseFloat);
                    reader.Required(base + ".noiseFrequency", payload.noiseFrequency, ParseFloat);
                } else if constexpr (std::is_same_v<T, ParticleVolumetricOutput>) {
                    reader.Required(base + ".density", payload.density, ParseFloat);
                    reader.Required(base + ".radiusScale", payload.radiusScale, ParseFloat);
                    reader.Required(base + ".lowQualitySteps", payload.lowQualitySteps, ParseUInt<std::uint32_t>);
                    reader.Required(base + ".highQualitySteps", payload.highQualitySteps, ParseUInt<std::uint32_t>);
                }
            },
            output.payload);
    }
    std::uint32_t eventCount = 0U;
    reader.RequiredBoundedCount("effect.eventBindingCount", eventCount, kParticleEffectMaxEventBindings);
    if (eventCount <= kParticleEffectMaxEventBindings)
        asset.eventBindings.resize(eventCount);
    for (std::size_t index = 0U; index < asset.eventBindings.size(); ++index) {
        auto& event = asset.eventBindings[index];
        reader.RequiredIndexAnchor(EventPath(index, "sourceEmitterId"), event.sourceEmitterId,
                                   ParseUInt<std::uint64_t>);
        reader.Required(EventPath(index, "trigger"), event.trigger, ParseEnum<ParticleEventTrigger>,
                        ParticleEffectDiagnosticCode::InvalidEnum);
        reader.Required(EventPath(index, "sourceModuleId"), event.sourceModuleId, ParseUInt<std::uint64_t>);
        reader.Required(EventPath(index, "action"), event.action, ParseEnum<ParticleEventAction>,
                        ParticleEffectDiagnosticCode::InvalidEnum);
        reader.Required(EventPath(index, "targetEmitterId"), event.targetEmitterId, ParseUInt<std::uint64_t>);
        ReadReference(reader, EventPath(index, "targetEffect"), event.targetEffect);
        reader.Required(EventPath(index, "count"), event.count, ParseUInt<std::uint32_t>);
        reader.Required(EventPath(index, "maxDepth"), event.maxDepth, ParseUInt<std::uint32_t>);
        reader.Required(EventPath(index, "perStepBudget"), event.perStepBudget, ParseUInt<std::uint32_t>);
    }
    reader.Finish();
    if (asset.formatVersion != kParticleEffectFormatVersion)
        Error(result, ParticleEffectDiagnosticCode::UnsupportedVersion, reader.ProvenanceLine("effect.formatVersion"),
              "effect.formatVersion", "future or unsupported version");
    if (result.diagnostics.empty()) {
        auto validation = ParticleEffectAssetValidator::ValidateStructure(asset);
        for (ParticleEffectDiagnostic& diagnostic : validation.diagnostics)
            diagnostic.line = reader.ProvenanceLine(diagnostic.propertyPath);
        result.diagnostics = std::move(validation.diagnostics);
    }
    AnnotateDiagnosticContext(result, asset);
    if (!result.diagnostics.empty())
        result.asset.reset();
    return result;
}

[[nodiscard]] ParticleEffectLoadResult ParseLegacy(std::string_view source) {
    ParticleEffectLoadResult result{};
    LegacyParticleEffectAsset legacy{};
    std::istringstream input{std::string{source}};
    std::string line;
    std::uint32_t lineNumber = 0U;
    std::set<std::string> scalarKeys;
    while (std::getline(input, line)) {
        ++lineNumber;
        std::string_view content = Trim(line);
        if (const auto comment = content.find('#'); comment != std::string_view::npos)
            content = Trim(content.substr(0U, comment));
        if (content.empty())
            continue;
        const auto split = content.find_first_of(" \t");
        const std::string key{content.substr(0U, split)};
        const std::string_view value =
            split == std::string_view::npos ? std::string_view{} : Trim(content.substr(split));
        const bool repeatable = key == "sizeCurveKeyframe" || key == "colorGradientStop";
        const bool scalar = key == "material" || key == "looping" || key == "durationSeconds" ||
                            key == "maxParticles" || key == "emissionRatePerSecond" || key == "startSpeedMin" ||
                            key == "startSpeedMax" || key == "startLifetimeMin" || key == "startLifetimeMax" ||
                            key == "directionX" || key == "directionY" || key == "directionZ" ||
                            key == "spreadDegrees" || key == "gravityScale";
        if (!scalar && !repeatable) {
            Error(result, ParticleEffectDiagnosticCode::UnknownKey, lineNumber, key,
                  "legacy property is not recognized");
            continue;
        }
        if (!repeatable && !scalarKeys.insert(key).second) {
            Error(result, ParticleEffectDiagnosticCode::DuplicateKey, lineNumber, key,
                  "legacy scalar property is duplicated");
            continue;
        }
        bool ok = true;
        if (key == "material")
            legacy.materialReference = std::string{value};
        else if (key == "looping")
            ok = ParseBool(value, legacy.looping);
        else if (key == "durationSeconds")
            ok = ParseFloat(value, legacy.durationSeconds);
        else if (key == "maxParticles")
            ok = ParseUInt(value, legacy.maxParticles);
        else if (key == "emissionRatePerSecond")
            ok = ParseFloat(value, legacy.emissionRatePerSecond);
        else if (key == "startSpeedMin")
            ok = ParseFloat(value, legacy.startSpeedMin);
        else if (key == "startSpeedMax")
            ok = ParseFloat(value, legacy.startSpeedMax);
        else if (key == "startLifetimeMin")
            ok = ParseFloat(value, legacy.startLifetimeMin);
        else if (key == "startLifetimeMax")
            ok = ParseFloat(value, legacy.startLifetimeMax);
        else if (key == "directionX")
            ok = ParseFloat(value, legacy.direction.x);
        else if (key == "directionY")
            ok = ParseFloat(value, legacy.direction.y);
        else if (key == "directionZ")
            ok = ParseFloat(value, legacy.direction.z);
        else if (key == "spreadDegrees")
            ok = ParseFloat(value, legacy.spreadDegrees);
        else if (key == "gravityScale")
            ok = ParseFloat(value, legacy.gravityScale);
        else if (key == "sizeCurveKeyframe") {
            if (legacy.sizeOverLifetime.keyframes.size() >= kParticleEffectMaxCurveKeys) {
                Error(result, ParticleEffectDiagnosticCode::LimitExceeded, lineNumber, key,
                      "legacy curve exceeds the schema hard limit");
                continue;
            }
            auto t = Tokens(value);
            kb::math::CurveKeyframe k{};
            ok = t.size() == 3U && ParseFloat(t[0], k.time) && ParseFloat(t[1], k.value) && ParseEasing(t[2], k.easing);
            if (ok)
                legacy.sizeOverLifetime.keyframes.push_back(k);
        } else if (key == "colorGradientStop") {
            if (legacy.colorOverLifetime.stops.size() >= kParticleEffectMaxGradientStops) {
                Error(result, ParticleEffectDiagnosticCode::LimitExceeded, lineNumber, key,
                      "legacy gradient exceeds the schema hard limit");
                continue;
            }
            auto t = Tokens(value);
            kb::math::GradientStop s{};
            ok = t.size() == 5U && ParseFloat(t[0], s.time) && ParseFloat(t[1], s.color.r) &&
                 ParseFloat(t[2], s.color.g) && ParseFloat(t[3], s.color.b) && ParseFloat(t[4], s.color.a);
            if (ok)
                legacy.colorOverLifetime.stops.push_back(s);
        }
        if (!ok)
            Error(result, ParticleEffectDiagnosticCode::InvalidValue, lineNumber, key,
                  "legacy property value is invalid");
    }
    static constexpr std::string_view required[] = {
        "material",      "looping",       "durationSeconds",  "maxParticles",     "emissionRatePerSecond",
        "startSpeedMin", "startSpeedMax", "startLifetimeMin", "startLifetimeMax", "directionX",
        "directionY",    "directionZ",    "spreadDegrees",    "gravityScale"};
    for (auto key : required)
        if (!scalarKeys.contains(std::string{key}))
            Error(result, ParticleEffectDiagnosticCode::MissingKey, 1U, std::string{key},
                  "required legacy property is missing");
    if (result.diagnostics.empty()) {
        result.asset = ParticleEffectAssetMigration::FromLegacy(legacy);
        auto validation = ParticleEffectAssetValidator::ValidateStructure(*result.asset);
        result.diagnostics = std::move(validation.diagnostics);
        result.migratedFromLegacy = true;
    }
    if (!result.diagnostics.empty())
        result.asset.reset();
    return result;
}

[[nodiscard]] bool ParseRecords(std::string_view source, Records& records, ParticleEffectLoadResult& result, bool& v2,
                                std::uint32_t& headerLine) {
    std::uint32_t lineNumber = 0U;
    bool foundFirst = false;
    std::size_t offset = 0U;
    while (offset < source.size()) {
        ++lineNumber;
        const std::size_t newline = source.find('\n', offset);
        const std::size_t end = newline == std::string_view::npos ? source.size() : newline;
        std::string_view content = Trim(source.substr(offset, end - offset));
        offset = newline == std::string_view::npos ? source.size() : newline + 1U;
        if (content.empty() || content.front() == '#')
            continue;
        if (!foundFirst) {
            foundFirst = true;
            headerLine = lineNumber;
            if (content == "21kb ParticleEffect 2") {
                v2 = true;
                continue;
            }
            if (content.starts_with("21kb ParticleEffect ")) {
                std::uint32_t version = 0U;
                const std::string_view versionText = content.substr(std::string_view{"21kb ParticleEffect "}.size());
                if (ParseUInt(versionText, version) && version > kParticleEffectFormatVersion)
                    Error(result, ParticleEffectDiagnosticCode::UnsupportedVersion, lineNumber, "effect.formatVersion",
                          "future version is unsupported");
                else
                    Error(result, ParticleEffectDiagnosticCode::InvalidHeader, lineNumber, "effect.formatVersion",
                          "versioned header is not canonical");
                return false;
            }
            v2 = false;
            return true;
        }
    }
    if (!foundFirst) {
        Error(result, ParticleEffectDiagnosticCode::EmptySource, 0U, "", "source contains no semantic records");
        return false;
    }

    std::size_t recordCount = 0U;
    lineNumber = 0U;
    offset = 0U;
    bool skippedHeader = false;
    while (offset < source.size()) {
        ++lineNumber;
        const std::size_t newline = source.find('\n', offset);
        const std::size_t end = newline == std::string_view::npos ? source.size() : newline;
        const std::string_view content = Trim(source.substr(offset, end - offset));
        offset = newline == std::string_view::npos ? source.size() : newline + 1U;
        if (content.empty() || content.front() == '#')
            continue;
        if (!skippedHeader) {
            skippedHeader = true;
            continue;
        }
        if (++recordCount > kParticleEffectMaxRecords) {
            Error(result, ParticleEffectDiagnosticCode::LimitExceeded, lineNumber, std::string{content},
                  "record count exceeds the schema-derived parser limit");
            return false;
        }
    }
    records.reserve(recordCount);

    lineNumber = 0U;
    offset = 0U;
    skippedHeader = false;
    while (offset < source.size()) {
        ++lineNumber;
        const std::size_t newline = source.find('\n', offset);
        const std::size_t end = newline == std::string_view::npos ? source.size() : newline;
        const std::string_view content = Trim(source.substr(offset, end - offset));
        offset = newline == std::string_view::npos ? source.size() : newline + 1U;
        if (content.empty() || content.front() == '#')
            continue;
        if (!skippedHeader) {
            skippedHeader = true;
            continue;
        }
        const auto split = content.find_first_of(" \t");
        if (split == std::string_view::npos) {
            Error(result, ParticleEffectDiagnosticCode::InvalidSyntax, lineNumber, std::string{content},
                  "property has no value");
            return false;
        }
        const std::string_view key = content.substr(0U, split);
        const std::string_view value = Trim(content.substr(split));
        records.push_back(Record{key, value, lineNumber, false});
    }
    std::sort(records.begin(), records.end(), [](const Record& left, const Record& right) {
        if (left.key != right.key)
            return left.key < right.key;
        return left.line < right.line;
    });
    for (std::size_t index = 1U; index < records.size(); ++index) {
        if (records[index - 1U].key == records[index].key) {
            Error(result, ParticleEffectDiagnosticCode::DuplicateKey, records[index].line,
                  std::string{records[index].key}, "property is duplicated");
            return false;
        }
    }
    return result.diagnostics.empty();
}

[[nodiscard]] std::string Quote(std::string_view value) {
    std::string out{"\""};
    for (char c : value) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
        }
    }
    out += '"';
    return out;
}
[[nodiscard]] std::string Float(float value) {
    char buffer[64]{};
    auto r = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general,
                           std::numeric_limits<float>::max_digits10);
    return r.ec == std::errc{} ? std::string(buffer, r.ptr) : std::string{};
}
void Line(std::string& out, std::string_view key, std::string_view value) {
    out += key;
    out += ' ';
    out += value;
    out += '\n';
}
void BoolLine(std::string& out, std::string_view key, bool value) {
    Line(out, key, value ? "true" : "false");
}
void VecLine(std::string& out, std::string_view key, kb::math::Vec3 v) {
    Line(out, key, Float(v.x) + " " + Float(v.y) + " " + Float(v.z));
}
void QuatLine(std::string& out, std::string_view key, kb::math::Quat v) {
    Line(out, key, Float(v.x) + " " + Float(v.y) + " " + Float(v.z) + " " + Float(v.w));
}
void WriteReference(std::string& out, const std::string& base, const ParticleAssetReference& ref) {
    Line(out, base + ".assetId", std::to_string(ref.assetId));
    Line(out, base + ".path", Quote(ref.virtualPath));
}
void WriteCurve(std::string& out, const std::string& base, const kb::math::Curve& curve) {
    Line(out, base + ".keyCount", std::to_string(curve.keyframes.size()));
    for (std::size_t i = 0; i < curve.keyframes.size(); ++i) {
        auto p = base + ".key[" + std::to_string(i) + "]";
        Line(out, p + ".time", Float(curve.keyframes[i].time));
        Line(out, p + ".value", Float(curve.keyframes[i].value));
        Line(out, p + ".easing", kb::math::ToString(curve.keyframes[i].easing));
    }
}
void WriteGradient(std::string& out, const std::string& base, const kb::math::Gradient& gradient) {
    Line(out, base + ".stopCount", std::to_string(gradient.stops.size()));
    for (std::size_t i = 0; i < gradient.stops.size(); ++i) {
        auto p = base + ".stop[" + std::to_string(i) + "]";
        Line(out, p + ".time", Float(gradient.stops[i].time));
        VecLine(out, p + ".rgb", {gradient.stops[i].color.r, gradient.stops[i].color.g, gradient.stops[i].color.b});
        Line(out, p + ".alpha", Float(gradient.stops[i].color.a));
    }
}
void WriteFlipbook(std::string& out, const std::string& base, const ParticleFlipbookAsset& value) {
    Line(out, base + ".columns", std::to_string(value.columns));
    Line(out, base + ".rows", std::to_string(value.rows));
    Line(out, base + ".framesPerSecond", Float(value.framesPerSecond));
    BoolLine(out, base + ".looping", value.looping);
}

} // namespace

ParticleEffectLoadResult ParticleEffectAssetIO::Parse(std::string_view source) {
    ParticleEffectLoadResult result{};
    if (source.size() > kParticleEffectMaxSourceBytes) {
        Error(result, ParticleEffectDiagnosticCode::SourceTooLarge, 0U, "", "source exceeds 512 KiB");
        return result;
    }
    if (!IsValidParticleEffectUtf8(source)) {
        Error(result, ParticleEffectDiagnosticCode::InvalidUtf8, 0U, "", "source is not valid UTF-8");
        return result;
    }
    Records records;
    bool v2 = false;
    std::uint32_t headerLine = 0U;
    if (!ParseRecords(source, records, result, v2, headerLine))
        return result;
    return v2 ? ParseV2(std::move(records), headerLine) : ParseLegacy(source);
}

ParticleEffectLoadResult ParticleEffectAssetIO::LoadDetailed(const std::filesystem::path& path) {
    ParticleEffectLoadResult result{};
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        Error(result, ParticleEffectDiagnosticCode::FileAccessFailed, 0U, "", "source existence check failed");
        return result;
    }
    if (!exists) {
        Error(result, ParticleEffectDiagnosticCode::FileNotFound, 0U, "", "source file does not exist");
        return result;
    }
    const bool regularFile = std::filesystem::is_regular_file(path, error);
    if (error || !regularFile) {
        Error(result, ParticleEffectDiagnosticCode::FileAccessFailed, 0U, "", "source is not a readable file");
        return result;
    }
    const std::uintmax_t fileSize = std::filesystem::file_size(path, error);
    if (error) {
        Error(result, ParticleEffectDiagnosticCode::FileAccessFailed, 0U, "", "source size query failed");
        return result;
    }
    if (fileSize == 0U) {
        Error(result, ParticleEffectDiagnosticCode::EmptySource, 0U, "", "source file is empty");
        return result;
    }
    if (fileSize > kParticleEffectMaxSourceBytes) {
        Error(result, ParticleEffectDiagnosticCode::SourceTooLarge, 0U, "", "source exceeds 512 KiB");
        return result;
    }
    std::string source(static_cast<std::size_t>(fileSize), '\0');
    std::ifstream input{path, std::ios::binary};
    if (!input.is_open()) {
        Error(result, ParticleEffectDiagnosticCode::FileAccessFailed, 0U, "", "source open failed");
        return result;
    }
    input.read(source.data(), static_cast<std::streamsize>(source.size()));
    if (input.gcount() != static_cast<std::streamsize>(source.size()) || input.bad()) {
        Error(result, ParticleEffectDiagnosticCode::FileAccessFailed, 0U, "", "source read failed");
        return result;
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        Error(result, ParticleEffectDiagnosticCode::FileAccessFailed, 0U, "", "source changed during bounded read");
        return result;
    }
    return Parse(source);
}
std::optional<ParticleEffectAsset> ParticleEffectAssetIO::Load(const std::filesystem::path& path) {
    return LoadDetailed(path).asset;
}

std::optional<std::string> ParticleEffectAssetIO::Serialize(const ParticleEffectAsset& asset,
                                                            std::vector<ParticleEffectDiagnostic>& diagnostics) {
    auto validation = ParticleEffectAssetValidator::ValidateStructure(asset);
    diagnostics = std::move(validation.diagnostics);
    if (!diagnostics.empty())
        return std::nullopt;
    std::string out = "21kb ParticleEffect 2\n";
    Line(out, "effect.formatVersion", std::to_string(asset.formatVersion));
    Line(out, "effect.id", std::to_string(asset.effectId));
    Line(out, "effect.displayName", Quote(asset.displayName));
    Line(out, "effect.recipeCategory", Quote(asset.recipeCategory));
    Line(out, "effect.determinismSeed", std::to_string(asset.determinismSeed));
    Line(out, "effect.durationSeconds", Float(asset.durationSeconds));
    BoolLine(out, "effect.looping", asset.looping);
    Line(out, "effect.backendPolicy", EnumText(asset.backendPolicy));
    Line(out, "effect.gpuCatchupPolicy", EnumText(asset.gpuCatchupPolicy));
    Line(out, "effect.emitterCount", std::to_string(asset.emitters.size()));
    for (std::size_t ei = 0; ei < asset.emitters.size(); ++ei) {
        const auto& e = asset.emitters[ei];
        Line(out, IndexPath(ei, "id"), std::to_string(e.emitterId));
        Line(out, IndexPath(ei, "authoringOrder"), std::to_string(e.authoringOrder));
        Line(out, IndexPath(ei, "name"), Quote(e.name));
        BoolLine(out, IndexPath(ei, "enabled"), e.enabled);
        VecLine(out, IndexPath(ei, "localPosition"), e.localPosition);
        QuatLine(out, IndexPath(ei, "localRotation"), e.localRotation);
        VecLine(out, IndexPath(ei, "localScale"), e.localScale);
        Line(out, IndexPath(ei, "maxParticles"), std::to_string(e.maxParticles));
        Line(out, IndexPath(ei, "simulationSpace"), EnumText(e.simulationSpace));
        Line(out, IndexPath(ei, "spawn.mode"), EnumText(e.spawn.mode));
        WriteCurve(out, IndexPath(ei, "spawn.rate"), e.spawn.rateOverTime);
        Line(out, IndexPath(ei, "spawn.burstCount"), std::to_string(e.spawn.bursts.size()));
        for (std::size_t bi = 0; bi < e.spawn.bursts.size(); ++bi) {
            auto p = IndexPath(ei, "spawn.burst[" + std::to_string(bi) + "]");
            Line(out, p + ".timeSeconds", Float(e.spawn.bursts[bi].timeSeconds));
            Line(out, p + ".count", std::to_string(e.spawn.bursts[bi].count));
        }
        Line(out, IndexPath(ei, "spawn.lifetimeMin"), Float(e.spawn.lifetimeMin));
        Line(out, IndexPath(ei, "spawn.lifetimeMax"), Float(e.spawn.lifetimeMax));
        Line(out, IndexPath(ei, "spawn.speedMin"), Float(e.spawn.speedMin));
        Line(out, IndexPath(ei, "spawn.speedMax"), Float(e.spawn.speedMax));
        VecLine(out, IndexPath(ei, "spawn.direction"), e.spawn.direction);
        Line(out, IndexPath(ei, "spawn.spreadDegrees"), Float(e.spawn.spreadDegrees));
        Line(out, IndexPath(ei, "spawn.randomization"), Float(e.spawn.randomization));
        Line(out, IndexPath(ei, "spawn.prewarmSeconds"), Float(e.spawn.prewarmSeconds));
        Line(out, IndexPath(ei, "moduleCount"), std::to_string(e.modules.size()));
        for (std::size_t mi = 0; mi < e.modules.size(); ++mi) {
            const auto& m = e.modules[mi];
            Line(out, ModulePath(ei, mi, "id"), std::to_string(m.moduleId));
            Line(out, ModulePath(ei, mi, "type"), EnumText(m.type));
            BoolLine(out, ModulePath(ei, mi, "enabled"), m.enabled);
            auto base = ModulePath(ei, mi, "payload");
            std::visit(
                [&](const auto& p) {
                    using T = std::decay_t<decltype(p)>;
                    if constexpr (std::is_same_v<T, ParticleInitialVelocityModule>) {
                        VecLine(out, base + ".direction", p.direction);
                        Line(out, base + ".speedMin", Float(p.speedMin));
                        Line(out, base + ".speedMax", Float(p.speedMax));
                        Line(out, base + ".randomization", Float(p.randomization));
                        Line(out, base + ".spreadDegrees", Float(p.spreadDegrees));
                    } else if constexpr (std::is_same_v<T, ParticleGravityModule>) {
                        VecLine(out, base + ".acceleration", p.acceleration);
                        Line(out, base + ".sceneGravityScale", Float(p.sceneGravityScale));
                    } else if constexpr (std::is_same_v<T, ParticleWindModule>)
                        VecLine(out, base + ".acceleration", p.acceleration);
                    else if constexpr (std::is_same_v<T, ParticleDragModule>)
                        Line(out, base + ".coefficient", Float(p.coefficient));
                    else if constexpr (std::is_same_v<T, ParticleColorOverLifeModule>)
                        WriteGradient(out, base + ".gradient", p.gradient);
                    else if constexpr (std::is_same_v<T, ParticleSizeOverLifeModule> ||
                                       std::is_same_v<T, ParticleAlphaOverLifeModule>)
                        WriteCurve(out, base + ".curve", p.curve);
                    else if constexpr (std::is_same_v<T, ParticleCollisionPlaneModule>) {
                        VecLine(out, base + ".normal", p.normal);
                        Line(out, base + ".distance", Float(p.distance));
                        Line(out, base + ".restitution", Float(p.restitution));
                        Line(out, base + ".friction", Float(p.friction));
                        Line(out, base + ".maxEventsPerStep", std::to_string(p.maxEventsPerStep));
                    } else if constexpr (std::is_same_v<T, ParticleSubEmitterModule>) {
                        Line(out, base + ".targetEmitterId", std::to_string(p.targetEmitterId));
                        Line(out, base + ".trigger", EnumText(p.trigger));
                        Line(out, base + ".count", std::to_string(p.count));
                        Line(out, base + ".maxDepth", std::to_string(p.maxDepth));
                    }
                },
                m.payload);
        }
        const auto& o = e.output;
        Line(out, IndexPath(ei, "output.type"), EnumText(o.type));
        WriteReference(out, IndexPath(ei, "output.material"), o.material);
        WriteReference(out, IndexPath(ei, "output.mesh"), o.mesh);
        WriteReference(out, IndexPath(ei, "output.textureAtlas"), o.textureAtlas);
        Line(out, IndexPath(ei, "output.blend"), EnumText(o.blend));
        Line(out, IndexPath(ei, "output.sort"), EnumText(o.sort));
        BoolLine(out, IndexPath(ei, "output.depthTest"), o.depthTest);
        BoolLine(out, IndexPath(ei, "output.depthWrite"), o.depthWrite);
        BoolLine(out, IndexPath(ei, "output.softParticles"), o.softParticles);
        BoolLine(out, IndexPath(ei, "output.antiAliasing"), o.antiAliasing);
        Line(out, IndexPath(ei, "output.alignment"), EnumText(o.alignment));
        auto base = IndexPath(ei, "output.payload");
        std::visit(
            [&](const auto& p) {
                using T = std::decay_t<decltype(p)>;
                if constexpr (std::is_same_v<T, ParticleBillboardOutput>)
                    WriteFlipbook(out, base + ".flipbook", p.flipbook);
                else if constexpr (std::is_same_v<T, ParticleStretchedBillboardOutput>) {
                    WriteFlipbook(out, base + ".flipbook", p.flipbook);
                    Line(out, base + ".velocityScale", Float(p.velocityScale));
                    Line(out, base + ".minimumLength", Float(p.minimumLength));
                } else if constexpr (std::is_same_v<T, ParticlePointSpriteOutput>) {
                    WriteFlipbook(out, base + ".flipbook", p.flipbook);
                    Line(out, base + ".diameter", Float(p.diameter));
                } else if constexpr (std::is_same_v<T, ParticleMeshOutput>) {
                    Line(out, base + ".lodBias", Float(p.lodBias));
                    BoolLine(out, base + ".castsShadow", p.castsShadow);
                    BoolLine(out, base + ".receivesShadow", p.receivesShadow);
                } else if constexpr (std::is_same_v<T, ParticleTrailOutput>) {
                    Line(out, base + ".sampleIntervalSeconds", Float(p.sampleIntervalSeconds));
                    Line(out, base + ".minimumDistance", Float(p.minimumDistance));
                    Line(out, base + ".maxSamplesPerParticle", std::to_string(p.maxSamplesPerParticle));
                    Line(out, base + ".width", Float(p.width));
                } else if constexpr (std::is_same_v<T, ParticleRibbonOutput>) {
                    Line(out, base + ".maxSegments", std::to_string(p.maxSegments));
                    Line(out, base + ".width", Float(p.width));
                    BoolLine(out, base + ".breakOnDeath", p.breakOnDeath);
                } else if constexpr (std::is_same_v<T, ParticleBeamOutput>) {
                    VecLine(out, base + ".localEnd", p.localEnd);
                    Line(out, base + ".segments", std::to_string(p.segments));
                    Line(out, base + ".width", Float(p.width));
                    Line(out, base + ".noiseAmplitude", Float(p.noiseAmplitude));
                    Line(out, base + ".noiseFrequency", Float(p.noiseFrequency));
                } else if constexpr (std::is_same_v<T, ParticleVolumetricOutput>) {
                    Line(out, base + ".density", Float(p.density));
                    Line(out, base + ".radiusScale", Float(p.radiusScale));
                    Line(out, base + ".lowQualitySteps", std::to_string(p.lowQualitySteps));
                    Line(out, base + ".highQualitySteps", std::to_string(p.highQualitySteps));
                }
            },
            o.payload);
    }
    Line(out, "effect.eventBindingCount", std::to_string(asset.eventBindings.size()));
    for (std::size_t i = 0; i < asset.eventBindings.size(); ++i) {
        const auto& e = asset.eventBindings[i];
        Line(out, EventPath(i, "sourceEmitterId"), std::to_string(e.sourceEmitterId));
        Line(out, EventPath(i, "trigger"), EnumText(e.trigger));
        Line(out, EventPath(i, "sourceModuleId"), std::to_string(e.sourceModuleId));
        Line(out, EventPath(i, "action"), EnumText(e.action));
        Line(out, EventPath(i, "targetEmitterId"), std::to_string(e.targetEmitterId));
        WriteReference(out, EventPath(i, "targetEffect"), e.targetEffect);
        Line(out, EventPath(i, "count"), std::to_string(e.count));
        Line(out, EventPath(i, "maxDepth"), std::to_string(e.maxDepth));
        Line(out, EventPath(i, "perStepBudget"), std::to_string(e.perStepBudget));
    }
    if (out.size() > kParticleEffectMaxSourceBytes) {
        diagnostics.push_back({.code = ParticleEffectDiagnosticCode::SourceTooLarge,
                               .propertyPath = "effect",
                               .message = "serialized source exceeds 512 KiB"});
        return std::nullopt;
    }
    return out;
}

ParticleEffectSaveResult ParticleEffectAssetIO::SaveDetailed(const std::filesystem::path& path,
                                                             const ParticleEffectAsset& asset) {
    ParticleEffectSaveResult result{};
    auto text = Serialize(asset, result.diagnostics);
    if (!text)
        return result;
    if (!SceneAssetBinaryIO::WriteBytesAtomically(path,
                                                  {reinterpret_cast<const std::uint8_t*>(text->data()), text->size()}))
        result.diagnostics.push_back({.code = ParticleEffectDiagnosticCode::AtomicWriteFailed,
                                      .propertyPath = path.string(),
                                      .message = "atomic asset replacement failed"});
    return result;
}
bool ParticleEffectAssetIO::Save(const std::filesystem::path& path, const ParticleEffectAsset& asset) {
    return SaveDetailed(path, asset).Succeeded();
}

} // namespace kb::scene
