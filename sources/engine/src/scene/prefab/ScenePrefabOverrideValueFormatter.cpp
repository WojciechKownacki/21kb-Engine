#include "scene/prefab/ScenePrefabOverrideValueFormatter.hpp"

#include <array>
#include <charconv>
#include <sstream>
#include <string_view>
#include <system_error>

namespace kb::scene {

bool ScenePrefabOverrideValueFormatter::Equal(Vec3 lhs, Vec3 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

bool ScenePrefabOverrideValueFormatter::Equal(Quat lhs, Quat rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

bool ScenePrefabOverrideValueFormatter::Equal(const kb::math::DVec3& lhs, const kb::math::DVec3& rhs) noexcept {
    return lhs == rhs;
}

std::string ScenePrefabOverrideValueFormatter::ToString(bool value) {
    return value ? "true" : "false";
}

std::string ScenePrefabOverrideValueFormatter::ToString(std::uint64_t value) {
    return std::to_string(value);
}

std::string ScenePrefabOverrideValueFormatter::ToString(float value) {
    std::ostringstream output;
    output << value;
    return output.str();
}

std::string ScenePrefabOverrideValueFormatter::ToString(Vec3 value) {
    std::ostringstream output;
    output << value.x << ' ' << value.y << ' ' << value.z;
    return output.str();
}

std::string ScenePrefabOverrideValueFormatter::ToString(Quat value) {
    std::ostringstream output;
    output << value.x << ' ' << value.y << ' ' << value.z << ' ' << value.w;
    return output.str();
}

namespace {

// The shortest text of the float when the value is one (every translation saved before double precision, and any
// without a part below float precision), the shortest text of the double otherwise: both read back exactly.
void AppendTranslationComponent(std::string& text, double value) {
    std::array<char, 32> buffer{};
    const float view = static_cast<float>(value);
    const std::to_chars_result result = static_cast<double>(view) == value
        ? std::to_chars(buffer.data(), buffer.data() + buffer.size(), view)
        : std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    text.append(buffer.data(), result.ptr);
}

// A token that is the shortest text of a float stands for that float exactly; any other is read as a double.
[[nodiscard]] bool ParseTranslationComponent(std::string_view token, double& output) {
    double value = 0.0;
    const std::from_chars_result parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) return false;
    const float view = static_cast<float>(value);
    std::array<char, 32> buffer{};
    const std::to_chars_result shortest = std::to_chars(buffer.data(), buffer.data() + buffer.size(), view);
    output = std::string_view{ buffer.data(), static_cast<std::size_t>(shortest.ptr - buffer.data()) } == token ? static_cast<double>(view) : value;
    return true;
}

} // namespace

std::string ScenePrefabOverrideValueFormatter::ToString(const kb::math::DVec3& value) {
    std::string text;
    AppendTranslationComponent(text, value.x);
    text.push_back(' ');
    AppendTranslationComponent(text, value.y);
    text.push_back(' ');
    AppendTranslationComponent(text, value.z);
    return text;
}

bool ScenePrefabOverrideValueFormatter::Parse(std::string_view text, kb::math::DVec3& output) {
    std::istringstream stream{ std::string{ text } };
    std::string x;
    std::string y;
    std::string z;
    std::string extra;
    return static_cast<bool>(stream >> x >> y >> z) && !(stream >> extra) &&
        ParseTranslationComponent(x, output.x) && ParseTranslationComponent(y, output.y) && ParseTranslationComponent(z, output.z);
}

} // namespace kb::scene
