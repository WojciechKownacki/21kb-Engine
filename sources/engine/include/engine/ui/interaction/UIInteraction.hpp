#pragma once

#include <cstdint>
#include <string>

namespace kb::scene {

enum class UINavigationMode : std::uint8_t {
    None,
    Automatic,
    Explicit,
};

struct UIInteraction {
    bool raycastTarget = true;
    bool interactable = true;
    UINavigationMode navigationMode = UINavigationMode::Automatic;
    std::uint64_t navigationUp = 0U;
    std::uint64_t navigationDown = 0U;
    std::uint64_t navigationLeft = 0U;
    std::uint64_t navigationRight = 0U;
    std::string eventName;
};

} // namespace kb::scene
