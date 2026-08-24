#pragma once

#include "ParticleEffectCompiler.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/ParticleEffectAssetValidation.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets {
class AssetRegistry;
struct AssetMetadata;
}

namespace kb::particle_editor {

enum class ParticleEditorProperty : std::uint16_t {
    SpawnRateCurve, SpawnBurstsSummary, SpawnLifetimeMin, SpawnLifetimeMax, SpawnSpeedMin, SpawnSpeedMax, SpawnDirection,
    SpawnSpreadDegrees, SpawnRandomization, SpawnPrewarmSeconds, SpawnStartColor, SpawnStartSize,
    OutputBlend, OutputSort, OutputDepthTest, OutputDepthWrite, OutputSoftParticles,
    OutputAntiAliasing, OutputAlignment, FlipbookColumns, FlipbookRows,
    FlipbookFramesPerSecond, FlipbookLooping, OutputVelocityScale, OutputMinimumLength,
    OutputPointDiameter, ModulePayload,
};

enum class ParticleEditorPropertyWidget : std::uint8_t {
    Text,
    Slider,
    IntegerSlider,
    Toggle,
    Enum,
    Vector,
    Curve,
    Gradient,
    Color,
};

struct ParticleEditorPropertyRow {
    ParticleEditorProperty property = ParticleEditorProperty::SpawnLifetimeMin;
    kb::scene::ParticleStableId moduleId = 0U;
    std::uint8_t payloadField = 0U;
    std::string label;
    std::string value;
    bool editable = true;
    ParticleEditorPropertyWidget widget = ParticleEditorPropertyWidget::Text;
    float numericMin = 0.0F;
    float numericMax = 1.0F;
    float numericValue = 0.0F;
    bool boolValue = false;
    std::uint32_t enumValue = 0U;
    std::array<const char*, 8> enumLabels{};
    std::uint8_t enumCount = 0U;
    kb::math::Vec3 vectorValue{};
    kb::math::Curve curve{};
    kb::math::Gradient gradient{};
    kb::math::Color colorValue{};
};

[[nodiscard]] constexpr bool IsParticleEditorSpawnProperty(ParticleEditorProperty property) noexcept {
    return property <= ParticleEditorProperty::SpawnStartSize;
}

[[nodiscard]] constexpr bool IsParticleEditorOutputProperty(ParticleEditorProperty property) noexcept {
    return property >= ParticleEditorProperty::OutputBlend && property <= ParticleEditorProperty::OutputPointDiameter;
}

struct ParticleOutputChoiceRow {
    kb::scene::ParticleOutputType type = kb::scene::ParticleOutputType::Billboard;
    std::string label;
    bool enabled = false;
    std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;
};

struct ParticleModuleStackRow {
    kb::scene::ParticleStableId moduleId = 0U;
    std::uint32_t authoringOrder = 0U;
    kb::scene::ParticleModuleType type = kb::scene::ParticleModuleType::InitialVelocity;
    std::string label;
    std::string summary;
    bool enabled = false;
    bool selected = false;
};

struct ParticleDependencyRow {
    kb::assets::AssetId assetId{};
    std::string virtualPath;
};

struct ParticleEmitterInspectorView {
    kb::scene::ParticleStableId emitterId = 0U;
    std::vector<ParticleOutputChoiceRow> outputChoices;
    std::vector<ParticleModuleStackRow> modules;
    std::vector<ParticleEditorPropertyRow> properties;
    std::vector<ParticleDependencyRow> dependencies;
    std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;
};

class ParticleEmitterInspectorModel final {
public:
    ParticleEmitterInspectorModel() = delete;
    [[nodiscard]] static std::string FormatColor(const kb::math::Color& color);
    [[nodiscard]] static std::string FormatGradient(const kb::math::Gradient& gradient);
    [[nodiscard]] static bool ParseColor(std::string_view text, kb::math::Color& value) noexcept;
    [[nodiscard]] static bool ParseVec3(std::string_view text, kb::math::Vec3& value) noexcept;
    [[nodiscard]] static bool ParseCurve(std::string_view text, kb::math::Curve& value);
    [[nodiscard]] static bool ParseGradient(std::string_view text, kb::math::Gradient& value);
    [[nodiscard]] static ParticleEmitterInspectorView Build(
        const kb::scene::ParticleEffectAsset& asset,
        kb::scene::ParticleStableId selectedEmitterId,
        kb::scene::ParticleStableId selectedModuleId,
        const kb::assets::AssetMetadata* owner,
        const kb::assets::AssetRegistry* registry,
        const kb::particle_plugin::ParticleCompileRequest& request = {});
};

} // namespace kb::particle_editor
