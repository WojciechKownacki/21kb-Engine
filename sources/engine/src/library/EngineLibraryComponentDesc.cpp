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
    // reloads all six), not guessed. The mechanism: SceneDocumentService::
    // Save captures the scene as a prefab (ScenePrefabs::CaptureRegistered)
    // and ScenePrefabBakedData::Bake (ScenePrefabBakedData.hpp) is what
    // actually serializes it — Transform and Visibility are baked
    // UNCONDITIONALLY per node (ScenePrefabBakedArchetype::transforms/
    // visibility, no mask bit), Camera/MeshRenderer/Light/Behaviour are
    // baked behind ScenePrefabBakedComponentMask bits. All six survive.
    // (An earlier version of this catalog wrongly marked Visibility
    // false, reasoning from SceneAssetComponentCodec — a DIFFERENT,
    // unrelated serialization path that does not cover Transform or
    // Visibility either; the round-trip test below caught the mistake.)
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
    };
    return kCatalog;
}

const LibraryComponentDesc* EngineLibraryComponentRegistry::Find(std::string_view name) noexcept {
    const std::vector<LibraryComponentDesc>& catalog = Catalog();
    const auto iterator = std::ranges::find_if(catalog, [name](const LibraryComponentDesc& desc) { return desc.name == name; });
    return iterator == catalog.end() ? nullptr : &*iterator;
}

} // namespace kb::library
