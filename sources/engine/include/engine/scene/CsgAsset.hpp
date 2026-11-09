#pragma once
#include "engine/assets/CollisionMeshAsset.hpp"
#include "engine/scene/SceneEntity.hpp"
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace kb::scene {
class Scene;
enum class CsgOperation { Union, Subtract, Intersect };
// Ordered left fold. The first brush seeds the solid and must use Union.
// Mesh brushes require closed, consistently outward-wound triangle meshes.
struct CsgBrush {
    CsgOperation operation = CsgOperation::Union;
    bool useMesh = false;
    std::array<float,3> size{1,1,1};
    std::array<float,3> position{};
    std::array<float,3> rotationDegrees{};
    std::array<float,3> scale{1,1,1};
    kb::assets::CollisionMeshAsset mesh;
};
struct CsgAsset { std::vector<CsgBrush> brushes; };
struct CompiledCsg { kb::assets::CollisionMeshAsset mesh; double volume = 0; };
[[nodiscard]] bool DecodeCsg(std::string_view source,CsgAsset& output,std::string& error);
[[nodiscard]] std::string EncodeCsg(const CsgAsset& source);
[[nodiscard]] bool CompileCsg(const CsgAsset& source,CompiledCsg& output,std::string& error);
// OBJ is encoded for the engine's existing right-handed source importer.
[[nodiscard]] bool WriteCsgObj(const std::filesystem::path& path,const CompiledCsg& compiled,std::string& error);
// Both assets are validated before persistent renderer/static-body components change.
[[nodiscard]] bool AttachCompiledCsg(Scene& scene,SceneEntity owner,std::string_view meshPath,std::string_view collisionPath,std::string& error);
}
