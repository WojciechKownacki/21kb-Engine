#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kb::scene {

struct TagsComponent {
    static constexpr std::string_view StableId = "kb21.scene.classification";
    static constexpr std::uint32_t SchemaVersion = 1U;
    static constexpr std::uint32_t MaxBytes = 255U;

    std::array<char, MaxBytes + 1U> tags{};
    std::uint32_t length = 0U;
};

inline std::string_view TagsText(const TagsComponent& component) noexcept {
    return std::string_view{ component.tags.data(), component.length };
}

inline bool TagsTextIsValid(std::string_view tags) noexcept {
    if (tags.size() > TagsComponent::MaxBytes) {
        return false;
    }
    for (const char character : tags) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte == 0U || byte < 0x20U || byte == 0x7FU || character == ',' || character == ';') {
            return false;
        }
    }
    return true;
}

inline void SetTagsText(TagsComponent& component, std::string_view tags) noexcept {
    const std::uint32_t length = static_cast<std::uint32_t>(std::min<std::size_t>(tags.size(), TagsComponent::MaxBytes));
    std::fill(component.tags.begin(), component.tags.end(), '\0');
    for (std::uint32_t index = 0U; index < length; ++index) {
        component.tags[index] = tags[index];
    }
    component.length = length;
}

inline bool TrySetTagsText(TagsComponent& component, std::string_view tags) noexcept {
    if (!TagsTextIsValid(tags)) {
        return false;
    }
    SetTagsText(component, tags);
    return true;
}

} // namespace kb::scene
