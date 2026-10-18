#include "engine/gameplay/AbilityAsset.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <memory>
#include <sstream>

namespace kb::gameplay {
namespace {

constexpr std::string_view kMagic = "21kb-ability 1";
constexpr std::size_t kMaxNameBytes = 128U;

struct DamageTypeEntry { DamageType type; std::string_view name; };
constexpr std::array<DamageTypeEntry, kDamageTypeCount> kDamageTypeNames{ {
    { DamageType::Physical, "Physical" }, { DamageType::Fire, "Fire" }, { DamageType::Healing, "Healing" },
    { DamageType::Cold, "Cold" }, { DamageType::Lightning, "Lightning" }, { DamageType::Poison, "Poison" },
} };

[[nodiscard]] bool ParseNumber(std::string_view text, float& output) {
    float value = 0.0F;
    const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (code != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) return false;
    output = value;
    return true;
}

[[nodiscard]] std::string Number(float value) {
    char buffer[32]{};
    const auto [end, code] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return code == std::errc{} ? std::string{ buffer, end } : std::string{ "0" };
}

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1U);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1U);
    return text;
}

} // namespace

std::string_view AbilityTargetName(AbilityTarget target) noexcept {
    switch (target) {
    case AbilityTarget::Enemy: return "Enemy";
    case AbilityTarget::Ally: return "Ally";
    case AbilityTarget::Self: return "Self";
    }
    return "Enemy";
}

std::optional<AbilityTarget> ParseAbilityTarget(std::string_view text) noexcept {
    if (text == "Enemy") return AbilityTarget::Enemy;
    if (text == "Ally") return AbilityTarget::Ally;
    if (text == "Self") return AbilityTarget::Self;
    return std::nullopt;
}

std::string_view DamageTypeName(DamageType type) noexcept {
    for (const auto& entry : kDamageTypeNames) {
        if (entry.type == type) return entry.name;
    }
    return "Physical";
}

std::optional<DamageType> ParseDamageType(std::string_view text) noexcept {
    for (const auto& entry : kDamageTypeNames) {
        if (entry.name == text) return entry.type;
    }
    return std::nullopt;
}

std::optional<std::string> ValidateAbilityAsset(const AbilityAsset& asset) {
    const auto finite = [](float value) { return std::isfinite(value) && value >= 0.0F; };
    if (asset.name.size() > kMaxNameBytes || asset.animatorTrigger.size() > kMaxNameBytes) return "name and animatorTrigger are at most 128 bytes";
    if (!finite(asset.range) || !finite(asset.radius) || !finite(asset.amount) || !finite(asset.manaCost) || !finite(asset.cooldownSeconds)) {
        return "range, radius, amount, manaCost and cooldown must be finite numbers, not negative";
    }
    if (asset.target == AbilityTarget::Self && asset.damageType != DamageType::Healing && asset.radius <= 0.0F) {
        return "a Self ability without a radius would hurt its caster: give it damageType Healing or a radius";
    }
    return std::nullopt;
}

bool AbilityAssetIO::Set(AbilityAsset& asset, std::string_view key, std::string_view value, std::string* error) {
    const auto fail = [&](std::string message) {
        if (error != nullptr) *error = std::move(message);
        return false;
    };
    if (key == "name") { asset.name = std::string{ value }; return true; }
    if (key == "animatorTrigger") { asset.animatorTrigger = std::string{ value }; return true; }
    if (key == "target") {
        const std::optional<AbilityTarget> target = ParseAbilityTarget(value);
        if (!target.has_value()) return fail("target must be Enemy, Ally or Self");
        asset.target = *target;
        return true;
    }
    if (key == "damageType") {
        const std::optional<DamageType> type = ParseDamageType(value);
        if (!type.has_value()) return fail("damageType must be Physical, Fire, Cold, Lightning, Poison or Healing");
        asset.damageType = *type;
        return true;
    }
    float* number = key == "range" ? &asset.range : key == "radius" ? &asset.radius : key == "amount" ? &asset.amount
        : key == "manaCost" ? &asset.manaCost : key == "cooldown" ? &asset.cooldownSeconds : nullptr;
    if (number == nullptr) {
        return fail("unknown key '" + std::string{ key } +
            "' (keys: name target range radius amount damageType manaCost cooldown animatorTrigger)");
    }
    if (!ParseNumber(value, *number) || *number < 0.0F) return fail(std::string{ key } + " expects a number, not negative");
    return true;
}

std::string AbilityAssetIO::Write(const AbilityAsset& asset) {
    std::string text{ kMagic };
    text += "\nname=" + asset.name;
    text += "\ntarget=" + std::string{ AbilityTargetName(asset.target) };
    text += "\nrange=" + Number(asset.range);
    text += "\nradius=" + Number(asset.radius);
    text += "\namount=" + Number(asset.amount);
    text += "\ndamageType=" + std::string{ DamageTypeName(asset.damageType) };
    text += "\nmanaCost=" + Number(asset.manaCost);
    text += "\ncooldown=" + Number(asset.cooldownSeconds);
    text += "\nanimatorTrigger=" + asset.animatorTrigger;
    text += "\n";
    return text;
}

std::optional<AbilityAsset> AbilityAssetIO::Load(std::span<const std::uint8_t> bytes, std::string* error) {
    const auto fail = [error](std::string message) -> std::optional<AbilityAsset> {
        if (error != nullptr) *error = std::move(message);
        return std::nullopt;
    };
    std::istringstream input{ std::string{ reinterpret_cast<const char*>(bytes.data()), bytes.size() } };
    std::string line;
    if (!std::getline(input, line) || Trim(line) != kMagic) return fail("not an ability file (it starts with '21kb-ability 1')");
    AbilityAsset asset;
    std::size_t lineNumber = 1U;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::string_view text = Trim(line);
        if (text.empty() || text.front() == '#') continue;
        const std::size_t equals = text.find('=');
        if (equals == std::string_view::npos) return fail("line " + std::to_string(lineNumber) + " is not key=value");
        std::string setError;
        if (!Set(asset, Trim(text.substr(0, equals)), Trim(text.substr(equals + 1U)), &setError)) {
            return fail("line " + std::to_string(lineNumber) + ": " + setError);
        }
    }
    if (const std::optional<std::string> invalid = ValidateAbilityAsset(asset)) return fail(*invalid);
    return asset;
}

std::optional<AbilityAsset> AbilityAssetIO::Load(const std::filesystem::path& path, std::string* error) {
    const std::vector<std::uint8_t> bytes = kb::scene::SceneAssetBinaryIO::ReadAllBytes(path);
    if (bytes.empty()) {
        if (error != nullptr) *error = "the file could not be read";
        return std::nullopt;
    }
    return Load(bytes, error);
}

bool AbilityAssetIO::Save(const std::filesystem::path& path, const AbilityAsset& asset) {
    if (ValidateAbilityAsset(asset).has_value()) return false;
    const std::string text = Write(asset);
    return kb::scene::SceneAssetBinaryIO::WriteBytesAtomically(
        path, std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
}

std::string_view AbilityAssetLoader::Type() const noexcept { return kAbilityAssetType; }
std::type_index AbilityAssetLoader::PayloadType() const noexcept { return typeid(AbilityAsset); }
std::vector<std::string> AbilityAssetLoader::Extensions() const { return { kAbilityAssetExtension }; }

kb::assets::AssetLoadResult AbilityAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    if (request.SourceExtension() != kAbilityAssetExtension) {
        return kb::assets::AssetLoadResult{ {}, "Ability asset has an unexpected file extension." };
    }
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!request.ReadSourceBytes(bytes, error)) return kb::assets::AssetLoadResult{ {}, std::move(error) };
    std::optional<AbilityAsset> asset = AbilityAssetIO::Load(bytes, &error);
    return asset ? kb::assets::AssetLoadResult{ std::make_shared<AbilityAsset>(std::move(*asset)), {} }
                 : kb::assets::AssetLoadResult{ {}, "Ability asset is invalid: " + error };
}

} // namespace kb::gameplay
