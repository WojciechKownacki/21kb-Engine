// FBX files through ufbx and the engine's conversion: source inspection on
#include "FuzzSupport.hpp"

// import and the skinned-mesh importer.
#include "engine/scene/SkeletalMeshFbxImporter.hpp"
#include "kb/render/resources/RenderMeshSourceImport.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::filesystem::path path = kb::fuzz::WriteScratchFile(data, size, "Mesh.fbx");
    std::string error;
    static_cast<void>(kb::render::RenderMeshSourceImport::Inspect(path, &error));
    static_cast<void>(kb::scene::SkeletalMeshFbxImporter::Import(path, 1U, {}, &error));
    return 0;
}
