#pragma once

#include "engine/assets/IAssetLoader.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"
#include <span>

namespace kb::scene {
class Scene;

enum class PhysicsAssemblyKind { Ragdoll, Cloth, Vehicle, Fracture, Buoyancy };
struct PhysicsBoneShape {
    std::string name;
    std::int32_t parent = -1;
    Vec3 center{};
    float radius = 0.12F, height = 0.6F, mass = 5.0F;
};
struct PhysicsAssemblyAsset {
    PhysicsAssemblyKind kind = PhysicsAssemblyKind::Cloth;
    std::vector<PhysicsBoneShape> bones;
    Vec3 size{1.6F,0.5F,3.0F};
    float mass = 700.0F;
    std::uint32_t columns = 8, rows = 8;
    float spacing = 0.2F, compliance = 0.00001F, bendingCompliance = 0.001F;
    bool pinTopEdge = true;
    float wheelRadius = 0.35F, suspensionLength = 0.4F, engineTorque = 1000.0F;
    std::uint32_t cellsX = 2, cellsY = 2, cellsZ = 2;
    float waterLevel = 0, buoyancy = 1.2F, linearDrag = 2.0F, angularDrag = 0.5F;
    Vec3 fluidVelocity{};
};

[[nodiscard]] std::string ValidatePhysicsAssembly(const PhysicsAssemblyAsset& asset);
[[nodiscard]] bool DecodePhysicsAssembly(std::string_view source, PhysicsAssemblyAsset& out, std::string& error);
[[nodiscard]] std::string EncodePhysicsAssembly(const PhysicsAssemblyAsset& asset);
[[nodiscard]] bool SavePhysicsAssembly(const std::filesystem::path& path, const PhysicsAssemblyAsset& asset, std::string& error);

class PhysicsAssemblyAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override { return "PhysicsAssembly"; }
    [[nodiscard]] std::type_index PayloadType() const noexcept override { return typeid(PhysicsAssemblyAsset); }
    [[nodiscard]] std::vector<std::string> Extensions() const override { return {".kbphysics"}; }
    [[nodiscard]] std::vector<kb::assets::AssetId> DiscoverDependencies(const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const override;
    [[nodiscard]] std::optional<std::string> ValidateDependencies(const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const override;
    [[nodiscard]] std::optional<std::string> ValidateRuntimeDependencies(const kb::assets::AssetLoadRequest& request, const kb::assets::AssetRegistry& registry) const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
};

struct PhysicsAssemblyResult {
    std::uint64_t handle = 0;
    std::string error;
    [[nodiscard]] bool Succeeded() const noexcept { return handle != 0 && error.empty(); }
};
struct PhysicsAssemblyState {
    PhysicsAssemblyKind kind = PhysicsAssemblyKind::Cloth;
    bool ready = false, simulated = true, fractured = false, submerged = false;
    float speed = 0;
    std::uint32_t wheelContacts = 0;
    std::vector<SceneEntity> entities;
    std::vector<Vec3> vertices;
};

// Scene-local handles are transient. Persist the asset and the script which instantiates it.
class IPhysicsAssemblyBackend {
public:
    virtual ~IPhysicsAssemblyBackend() = default;
    [[nodiscard]] virtual PhysicsAssemblyResult Create(SceneEntity owner, const PhysicsAssemblyAsset& asset) = 0;
    [[nodiscard]] virtual bool Destroy(std::uint64_t handle, std::string& error) = 0;
    [[nodiscard]] virtual bool State(std::uint64_t handle, PhysicsAssemblyState& out, std::string& error) = 0;
    [[nodiscard]] virtual bool Drive(std::uint64_t handle, float throttle, float steering, float brake, std::string& error) = 0;
    [[nodiscard]] virtual bool SimulateRagdoll(std::uint64_t handle, bool enabled, std::string& error) = 0;
    [[nodiscard]] virtual bool Fracture(std::uint64_t handle, Vec3 impulse, std::string& error) = 0;
    [[nodiscard]] virtual bool SetWaterLevel(std::uint64_t handle, float level, std::string& error) = 0;
};

class PhysicsAssemblies final {
public:
    static void RegisterBackend(Scene& scene, IPhysicsAssemblyBackend& backend);
    static void UnregisterBackend(Scene& scene, IPhysicsAssemblyBackend& backend) noexcept;
    [[nodiscard]] static PhysicsAssemblyResult Create(Scene& scene, SceneEntity owner, const PhysicsAssemblyAsset& asset);
    [[nodiscard]] static PhysicsAssemblyResult Load(Scene& scene, SceneEntity owner, std::string_view assetPath);
    [[nodiscard]] static bool Destroy(Scene& scene, std::uint64_t handle, std::string& error);
    [[nodiscard]] static bool State(Scene& scene, std::uint64_t handle, PhysicsAssemblyState& out, std::string& error);
    [[nodiscard]] static bool Drive(Scene& scene, std::uint64_t handle, float throttle, float steering, float brake, std::string& error);
    [[nodiscard]] static bool SimulateRagdoll(Scene& scene, std::uint64_t handle, bool enabled, std::string& error);
    [[nodiscard]] static bool Fracture(Scene& scene, std::uint64_t handle, Vec3 impulse, std::string& error);
    [[nodiscard]] static bool SetWaterLevel(Scene& scene, std::uint64_t handle, float level, std::string& error);
};
} // namespace kb::scene
