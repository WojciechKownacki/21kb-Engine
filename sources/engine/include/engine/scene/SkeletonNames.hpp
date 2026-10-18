#pragma once

#include <string>
#include <string_view>

namespace kb::scene {

// A bone or socket name as people mean it, whatever tool wrote it: the namespace a DCC puts in
// front ("mixamorig:", "Armature|") is dropped, case is ignored and separators are not
// characters, so "hand.R", "Hand_R" and "mixamorig:hand_r" are all "handr".
[[nodiscard]] inline std::string NormalizeSkeletonName(std::string_view name) {
    const std::size_t separator = name.find_last_of(":|");
    if (separator != std::string_view::npos && separator + 1U < name.size()) name.remove_prefix(separator + 1U);
    std::string normalized;
    normalized.reserve(name.size());
    for (const char character : name) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (value >= 'A' && value <= 'Z') normalized.push_back(static_cast<char>(value - 'A' + 'a'));
        else if ((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value >= 0x80U) normalized.push_back(character);
    }
    return normalized;
}

} // namespace kb::scene
