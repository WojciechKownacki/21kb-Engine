#pragma once

#include "engine/assets/IAssetLoader.hpp"
#include "engine/gameplay/Damage.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace kb::gameplay {

inline constexpr const char* kAbilityAssetExtension = ".kbability";
inline constexpr const char* kAbilityAssetType = "Ability";

// Whom an ability is used on: an enemy or an ally in range (or every one around the target point
// when it has a radius), or the caster itself.
enum class AbilityTarget : std::uint8_t { Enemy, Ally, Self };

// One thing a combatant can do - a sword swing, a fireball, a heal - as data a designer edits:
// a text file of `key=value` lines (`kb_cli ability new/set/show`). `amount` is the damage dealt
// (healed with damageType Healing); a positive `radius` hits everyone of the right side within it
// of the target. `animatorTrigger` fires on the caster's Animator, so the swing plays.
struct AbilityAsset {
    std::string name;
    AbilityTarget target = AbilityTarget::Enemy;
    float range = 2.0F;
    float radius = 0.0F;
    float amount = 10.0F;
    DamageType damageType = DamageType::Physical;
    float manaCost = 0.0F;
    float cooldownSeconds = 1.0F;
    std::string animatorTrigger;
};

[[nodiscard]] std::optional<std::string> ValidateAbilityAsset(const AbilityAsset& asset);
[[nodiscard]] std::string_view AbilityTargetName(AbilityTarget target) noexcept;
[[nodiscard]] std::optional<AbilityTarget> ParseAbilityTarget(std::string_view text) noexcept;
[[nodiscard]] std::string_view DamageTypeName(DamageType type) noexcept;
[[nodiscard]] std::optional<DamageType> ParseDamageType(std::string_view text) noexcept;

class AbilityAssetIO final {
public:
    AbilityAssetIO() = delete;
    [[nodiscard]] static std::optional<AbilityAsset> Load(const std::filesystem::path& path, std::string* error = nullptr);
    [[nodiscard]] static std::optional<AbilityAsset> Load(std::span<const std::uint8_t> bytes, std::string* error = nullptr);
    [[nodiscard]] static std::string Write(const AbilityAsset& asset);
    // Applies one `key=value` (the keys Write prints); false with `error` for an unknown key or value.
    [[nodiscard]] static bool Set(AbilityAsset& asset, std::string_view key, std::string_view value, std::string* error = nullptr);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const AbilityAsset& asset);
};

class AbilityAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
};

} // namespace kb::gameplay
