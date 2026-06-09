#pragma once

#include "kb/render/resources/RenderResources.hpp"

#include <cstdint>

namespace kb::render {

class RenderResourceRegistry;
class SceneRenderResourceMap;

struct SceneMaterialTextureDependencyDesc {
    const RenderMaterialResource* material = nullptr;
    const RenderResourceRegistry* resources = nullptr;
    const SceneRenderResourceMap* resourceMap = nullptr;
};

class SceneMaterialTextureDependencySignature {
public:
    SceneMaterialTextureDependencySignature() = delete;

    [[nodiscard]] static std::uint64_t Build(const SceneMaterialTextureDependencyDesc& desc) noexcept;
};

} // namespace kb::render
