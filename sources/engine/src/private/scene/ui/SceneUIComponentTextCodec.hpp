#pragma once

#include "engine/ui/UIComponentSet.hpp"

#include <string>
#include <string_view>

namespace kb::scene {

class SceneUIComponentTextCodec final {
public:
    SceneUIComponentTextCodec() = delete;

    [[nodiscard]] static std::string Encode(const UIComponentSet& components);
    [[nodiscard]] static bool Decode(std::string_view encoded, UIComponentSet& output);
    // The scene format version whose layout `encoded` uses; text without a version prefix predates
    // recorded versions and has the v34 layout. Returns 0 for text that names no valid version.
    [[nodiscard]] static std::uint32_t EncodedVersion(std::string_view encoded) noexcept;
};

} // namespace kb::scene
