#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;
}

namespace kb::assets {
class AssetManager;
}

namespace kb::scene {

// LIB-140: mirrors kb::render::RenderMaterialParameterType's scalar-shaped subset -
// kb::scene never depends on kb::render (see MaterialParameterOverride's own doc comment),
// so this is a deliberately independent, parallel enum, not a shared one - the same "two
// parallel enums crossing the boundary as plain values" pattern LIB-136 already established
// for CameraClearMode/RenderCameraClearMode. Vec3/Vec4/Color/Texture/Enum parameters are
// explicitly out of scope for LIB-140 (see ScriptMaterialInstanceApi.hpp's doc comment) -
// Scalar/Bool are the two parameter shapes that map 1:1 onto a single ScriptValue argument
// with no new multi-argument calling convention needed.
enum class MaterialParameterType : std::uint8_t {
    Scalar,
    Bool,
};

// LIB-140: one named parameter override on a runtime MaterialInstance - kb::scene's own
// plain-data mirror of kb::render::RenderMaterialGraphParameterValue's scalar/bool subset.
// `name` is the material graph's stableId string (the SAME identifier a `.kbmatgraph`
// author gives an exposed parameter node, or the synthetic node-id-derived fallback string
// when left blank - kb::render::RenderMaterialParameterSchema::name IS this string, see
// RenderMaterialGraphCompiler.cpp's BuildRenderMaterialGraphParameterSchema). kb::scene
// does not parse that schema itself (the renderer remains its single owner). Mutation goes
// through MaterialParameterSchemaValidator, a renderer-installed bridge, so storage contains
// only names/types accepted against the authoritative graph schema. The render-resolution
// path validates again as defense against malformed serialized/internal state.
struct MaterialParameterOverride {
    std::string name;
    MaterialParameterType type = MaterialParameterType::Scalar;
    float scalarValue = 0.0F;
    bool boolValue = false;
};

// Renderer-owned schema bridge used by SceneMaterialInstances at the mutation boundary.
// The engine owns only this narrow contract; the renderer remains the single source of
// truth for material graph parsing and parameter schemas. Implementations must not retain
// references to `name`.
class MaterialParameterSchemaValidator {
public:
    virtual ~MaterialParameterSchemaValidator() = default;

    [[nodiscard]] virtual bool Validate(
        const kb::assets::AssetManager& assets,
        std::uint64_t parentMaterialAssetId,
        std::string_view name,
        MaterialParameterType type) const noexcept = 0;
};

// LIB-139: read-only half of SceneMaterialInstances, mirroring
// SceneMeshRendererComponentQueries/SceneMeshRendererComponents' const/mutable
// split - obtained from a `const Scene&`, so it is safe to call from contexts
// that only ever see the scene read-only (e.g. kb::render's
// EcsRenderSceneSynchronizer, which never mutates kb::scene state).
class SceneMaterialInstanceQueries {
public:
    explicit SceneMaterialInstanceQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Exists(std::uint64_t id) const noexcept;
    // Returns 0 (never a valid asset id) if `id` names no currently live
    // instance.
    [[nodiscard]] std::uint64_t Parent(std::uint64_t id) const noexcept;
    // LIB-140: empty if `id` names no currently live instance, or the instance has no
    // overrides set. Read by kb::render's material-instance resolution path (never by
    // kb::scene/kb::script itself) to build the merged parameter set for a resolved
    // material.
    [[nodiscard]] std::span<const MaterialParameterOverride> Parameters(std::uint64_t id) const noexcept;

private:
    const Scene& scene_;
};

// LIB-139: MaterialInstance.Create/Release's engine-side facade. A runtime
// MaterialInstance is a scene-owned, explicit-lifetime indirection to a
// parent material asset - it exists so a script can hold a handle distinct
// from the shared parent asset (LIB-140 adds per-parameter overrides on top;
// LIB-139 alone already makes assigning a "private" reference to a
// MeshRenderer meaningful, since Release()ing it is observably different
// from never having created one, and a MeshRenderer referencing a released
// instance honestly falls back to no material rather than silently keeping
// the parent - see EcsRenderSceneSynchronizer::SyncMesh).
//
// `id` is a monotonically increasing per-scene std::uint64_t (SceneState::
// nextMaterialInstanceId), never reused within a scene's lifetime - the same
// convention as SceneTimers' TimerHandle, deliberately NOT a
// generation-checked handle registry for the same reason documented on
// SceneTimers.hpp: ids are never reused, so a stale id can never collide
// with a live one.
class SceneMaterialInstances {
public:
    explicit SceneMaterialInstances(Scene& scene) noexcept;

    // Returns 0 (never a valid id) if parentMaterialAssetId==0, or if the
    // scene already holds kMaxLiveMaterialInstances live instances (LIB-139's
    // "limit wariantów" - see SceneMaterialInstanceService.cpp).
    [[nodiscard]] std::uint64_t Create(std::uint64_t parentMaterialAssetId) noexcept;
    // Idempotent - false if `id` names no currently live instance (already
    // released, or never existed).
    [[nodiscard]] bool Release(std::uint64_t id) noexcept;
    [[nodiscard]] bool Exists(std::uint64_t id) const noexcept;
    [[nodiscard]] std::uint64_t Parent(std::uint64_t id) const noexcept;
    [[nodiscard]] std::span<const MaterialParameterOverride> Parameters(std::uint64_t id) const noexcept;
    // Installs the renderer-owned schema bridge. Scene ownership makes its lifetime explicit
    // and prevents dangling callbacks during renderer/plugin unload.
    void SetParameterSchemaValidator(std::shared_ptr<const MaterialParameterSchemaValidator> validator) noexcept;
    [[nodiscard]] bool HasParameterSchemaValidator() const noexcept;
    // Upserts a named override only after the installed validator confirms that `name`
    // exists on the parent graph, has the requested type, supports runtime overrides and is
    // runtime-supported. False for a stale handle, missing validator or invalid schema entry.
    [[nodiscard]] bool SetParameterScalar(std::uint64_t id, std::string_view name, float value) noexcept;
    [[nodiscard]] bool SetParameterBool(std::uint64_t id, std::string_view name, bool value) noexcept;
    // Idempotent - false if `id` names no currently live instance, or `name` has no
    // override set.
    [[nodiscard]] bool ClearParameter(std::uint64_t id, std::string_view name) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
