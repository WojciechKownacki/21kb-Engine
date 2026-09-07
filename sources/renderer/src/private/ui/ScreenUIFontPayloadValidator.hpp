#pragma once

#include "engine/assets/ImportedAsset.hpp"

#include <cstddef>
#include <string_view>

namespace kb::render {

class ScreenUIFontPayloadValidator {
  public:
    static constexpr std::size_t kMinimumPayloadBytes = 64U;
    static constexpr std::size_t kMaximumPayloadBytes = 64U * 1024U * 1024U;

    [[nodiscard]] static bool SupportsExtension(std::string_view extension) noexcept;
    [[nodiscard]] static bool Validate(const kb::assets::ImportedAsset& asset);
};

} // namespace kb::render
