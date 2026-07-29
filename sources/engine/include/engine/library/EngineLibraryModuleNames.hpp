#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace kb::library {

enum class LibraryModuleName : std::uint8_t {
    World,
    Entity,
    Transform,
    Time,
    Math,
    Input,
    Physics,
    Audio,
    Assets,
};

struct LibraryModuleNameDesc {
    LibraryModuleName value;
    std::string_view name;
    bool scriptNamespace = false;
};

// Canonical public vocabulary shared by C++, Lua and Visual Graph. Entity is
// native-only today: EntityHandle is intentionally a C++ value type, while
// script-visible entity lifecycle functions remain in the World namespace.
inline constexpr std::array<LibraryModuleNameDesc, 9U> kCanonicalLibraryModuleNames{{
    { LibraryModuleName::World, "World", true },
    { LibraryModuleName::Entity, "Entity", false },
    { LibraryModuleName::Transform, "Transform", true },
    { LibraryModuleName::Time, "Time", true },
    { LibraryModuleName::Math, "Math", true },
    { LibraryModuleName::Input, "Input", true },
    { LibraryModuleName::Physics, "Physics", true },
    { LibraryModuleName::Audio, "Audio", true },
    { LibraryModuleName::Assets, "Assets", true },
}};

[[nodiscard]] constexpr std::string_view ToString(LibraryModuleName module) noexcept {
    for (const LibraryModuleNameDesc& desc : kCanonicalLibraryModuleNames) {
        if (desc.value == module) {
            return desc.name;
        }
    }
    return {};
}

[[nodiscard]] constexpr bool IsCanonicalLibraryModuleName(std::string_view name) noexcept {
    for (const LibraryModuleNameDesc& desc : kCanonicalLibraryModuleNames) {
        if (desc.name == name) {
            return true;
        }
    }
    return false;
}

} // namespace kb::library
