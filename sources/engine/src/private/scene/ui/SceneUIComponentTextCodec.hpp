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
};

} // namespace kb::scene
