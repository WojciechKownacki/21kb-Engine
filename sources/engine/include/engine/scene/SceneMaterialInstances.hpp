#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

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
// cannot validate `name`/`type` against the real schema (that data lives entirely in
// kb::render, compiled from the parent material's graph) - storage here is deliberately
// unvalidated; real validation happens once, lazily, at render-resolution time
// (RuntimeMaterialResourceEnsurer's material-instance path) and an unresolvable/wrong-type
// override is silently dropped there, exactly mirroring how an unresolvable materialAssetId
// already silently falls back to "no material" rather than crashing.
struct MaterialParameterOverride {
    std::string name;
    MaterialParameterType type = MaterialParameterType::Scalar;
    float scalarValue = 0.0F;
    bool boolValue = false;
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
    // LIB-140: upserts a named override by `name` (last-write-wins, mirrors
    // RegisterEvent's own established upsert-by-key convention). False if `id` names no
    // currently live instance - true otherwise, even though kb::scene cannot yet confirm
    // `name` is a real parameter on the parent material's graph (see
    // MaterialParameterOverride's own doc comment for why, and where the real check
    // happens).
    [[nodiscard]] bool SetParameterScalar(std::uint64_t id, std::string_view name, float value) noexcept;
    [[nodiscard]] bool SetParameterBool(std::uint64_t id, std::string_view name, bool value) noexcept;
    // Idempotent - false if `id` names no currently live instance, or `name` has no
    // override set.
    [[nodiscard]] bool ClearParameter(std::uint64_t id, std::string_view name) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
