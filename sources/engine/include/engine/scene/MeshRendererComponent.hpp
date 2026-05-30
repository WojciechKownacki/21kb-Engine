#pragma once

#include <cstdint>

namespace kb::scene {

struct MeshRendererComponent {
    std::uint64_t meshAssetId = 0;
    std::uint64_t materialAssetId = 0;
    bool castsShadow = true;
    bool receivesShadow = true;
};

} // namespace kb::scene
