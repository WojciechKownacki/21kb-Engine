#include "engine/library/EngineLibraryComponentDesc.hpp"

#include <algorithm>

namespace kb::library {

LibraryComponentId ComputeLibraryComponentId(std::string_view name) noexcept {
    // FNV-1a 64-bit — same algorithm as ComputeLibraryFunctionId
    // (EngineLibraryFunctionId.cpp), kept as an independent implementation
    // here for the same reason that one is: this identifies a component
    // within kb::library's own contract, not an asset or a manifest blob.
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const unsigned char byte : name) {
        hash ^= byte;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

const std::vector<LibraryComponentDesc>& EngineLibraryComponentRegistry::Catalog() {
    // Every name here matches kb::script::ScriptSceneComponentApi::
    // ComponentNames() and one of LIB-075's ScriptComponentAccess<T>
    // specializations exactly — verified by
    // RunEngineLibraryComponentRegistryTest, which cross-checks this list
    // against ScriptSceneComponentApi::ComponentNames() at runtime so the
    // two cannot silently drift apart.
    //
    // `serializable` is verified against the REAL scene save/load round
    // trip (RunEngineLibraryComponentRegistryTest actually saves and
    // reloads every component marked true), not guessed. The mechanism:
    // SceneDocumentService::Save captures the scene as a prefab
    // (ScenePrefabs::CaptureRegistered) and ScenePrefabBakedData::Bake
    // (ScenePrefabBakedData.hpp) is what actually serializes it — Transform
    // and Visibility are baked UNCONDITIONALLY per node
    // (ScenePrefabBakedArchetype::transforms/visibility, no mask bit),
    // Camera/MeshRenderer/Light/Behaviour/Rigidbody/Collider/
    // CharacterController/Joint are baked behind ScenePrefabBakedComponentMask
    // bits. (An earlier version of this catalog wrongly marked
    // Visibility false, reasoning from SceneAssetComponentCodec — a
    // DIFFERENT, unrelated serialization path that does not cover Transform
    // or Visibility either; the round-trip test below caught the mistake.)
    static const std::vector<LibraryComponentDesc> kCatalog{
        LibraryComponentDesc{
            .name = "Transform",
            .id = ComputeLibraryComponentId("Transform"),
            .serializable = true,
        },
        LibraryComponentDesc{
            .name = "Visibility",
            .id = ComputeLibraryComponentId("Visibility"),
            .serializable = true,
        },
        LibraryComponentDesc{
            .name = "Camera",
            .id = ComputeLibraryComponentId("Camera"),
            .serializable = true,
        },
        LibraryComponentDesc{
            .name = "Light",
            .id = ComputeLibraryComponentId("Light"),
            .serializable = true,
        },
        LibraryComponentDesc{
            .name = "MeshRenderer",
            .id = ComputeLibraryComponentId("MeshRenderer"),
            .serializable = true,
        },
        LibraryComponentDesc{
            .name = "Behaviour",
            .id = ComputeLibraryComponentId("Behaviour"),
            .serializable = true,
        },
        // LIB-123: Rigidbody/Collider already had a real, tested prefab-bake
        // round trip (ScenePrefabBakedComponentMask::Rigidbody/Collider,
        // predating this catalog entry) - extending this catalog to them
        // only adds the script-facing surface, not new serialization.
        // CharacterController's bake wiring was added alongside this catalog
        // entry (RunEngineLibraryComponentRegistryTest exercises it below).
        LibraryComponentDesc{
            .name = "Rigidbody",
            .id = ComputeLibraryComponentId("Rigidbody"),
            .serializable = true,
        },
        LibraryComponentDesc{
            .name = "Collider",
            .id = ComputeLibraryComponentId("Collider"),
            .serializable = true,
        },
        LibraryComponentDesc{
            .name = "CharacterController",
            .id = ComputeLibraryComponentId("CharacterController"),
            .serializable = true,
        },
        // LIB-123: persistent prefab data stores a stable node id and only
        // resolves the live SceneEntity after the complete instance exists.
        LibraryComponentDesc{
            .name = "Joint",
            .id = ComputeLibraryComponentId("Joint"),
            .serializable = true,
        },
        LibraryComponentDesc{
            .name = "NavAgent",
            .id = ComputeLibraryComponentId("NavAgent"),
            .serializable = true,
        },
        LibraryComponentDesc{
            .name = "NavObstacle",
            .id = ComputeLibraryComponentId("NavObstacle"),
            .serializable = true,
        },
        LibraryComponentDesc{
            .name = "Tags",
            .id = ComputeLibraryComponentId("Tags"),
            .serializable = true,
        },
    };
    return kCatalog;
}

const LibraryComponentDesc* EngineLibraryComponentRegistry::Find(std::string_view name) noexcept {
    const std::vector<LibraryComponentDesc>& catalog = Catalog();
    const auto iterator = std::ranges::find_if(catalog, [name](const LibraryComponentDesc& desc) { return desc.name == name; });
    return iterator == catalog.end() ? nullptr : &*iterator;
}

} // namespace kb::library
