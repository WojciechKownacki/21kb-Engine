#pragma once
#include "engine/scene/VectorShape2DAsset.hpp"
#include "engine/scene/SceneEntity.hpp"
#include <optional>
namespace kb::scene {
class Scene;
class SceneVectorShapes2D final {
public:
    [[nodiscard]] static bool Set(Scene& scene,SceneEntity owner,const VectorShape2DAsset& shape,std::string& error);
    [[nodiscard]] static bool Load(Scene& scene,SceneEntity owner,std::string_view assetPath,std::string& error);
    [[nodiscard]] static std::optional<VectorShape2DAsset> Get(const Scene& scene,SceneEntity owner);
    [[nodiscard]] static bool SetPoint(Scene& scene,SceneEntity owner,std::uint32_t index,std::array<float,2> point,std::string& error);
    [[nodiscard]] static bool SetWidth(Scene& scene,SceneEntity owner,float width,std::string& error);
    [[nodiscard]] static bool Clear(Scene& scene,SceneEntity owner,std::string& error);
};
}
