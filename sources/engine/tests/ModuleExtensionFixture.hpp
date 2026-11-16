#pragma once

#include <cstdint>
#include <vector>

namespace kb::tests {

// The component the extension fixture module registers: three fields of different types.
struct ModuleExtensionComponent {
    float strength = 1.5F;
    std::int32_t count = 3;
    bool armed = true;
};

// What the fixture's `.kbtest` loader produces: the file's bytes. A file starts with eight bytes naming the asset it
// depends on (zero for none).
struct ModuleExtensionAsset {
    std::vector<std::uint8_t> bytes;
};

} // namespace kb::tests
