#include "CliCommands.hpp"
#include "CliMaterialShared.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/CsgAsset.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#if KB_CLI_RENDERER
#include "kb/render/resources/RenderMeshImportArtifacts.hpp"
#endif
#include <array>
#include <fstream>
#include <iterator>
namespace kb::cli {
int RunCsgCommand(const ArgumentList& args,CommandIo io) {
    const auto root=material::ProjectRoot(args,"csg",io);if(root.empty()) return 1;
    const auto fail=[&](const std::string& e){io.err<<"error: "<<e<<'\n';return 1;};const auto& words=args.Positionals();const auto action=words.empty()?std::string{}:words.front();std::string error,path;std::filesystem::path file;kb::scene::CsgAsset source;
    if(action=="new") {
        auto name=args.Option("--name");if(!name) return fail("csg new needs --name");error=kb::assets::AssetFileNameError(*name);if(!error.empty()) return fail(error);
        auto folder=kb::assets::ResolveProjectAssetFolder(root,args.Option("--folder").value_or("/Game/Geometry"));if(!folder.error.empty()) return fail(folder.error);file=folder.folder/kb::assets::Utf8Path(*name+".kbcsg");path=folder.virtualPath+"/"+*name+".kbcsg";if(std::filesystem::exists(file)) return fail("CSG source already exists");source.brushes.push_back({});
    }else if(action=="show"||action=="set"||action=="build") {
        auto requested=args.Option("--source");if(!requested) return fail("csg show/set/build needs --source /Game/<path>.kbcsg");auto located=material::ProjectAssetFile(root,*requested,".kbcsg","CSG source","create with csg new",io);if(!located) return 1;file=located->file;path=located->virtualPath;std::ifstream input(file,std::ios::binary);std::string text{std::istreambuf_iterator<char>{input},{}};if(!input||!kb::scene::DecodeCsg(text,source,error)) return fail(error);
    }else return fail("csg expects new, show, set or build");
    if(action=="set"||action=="new") {
        auto data=args.Option("--data");auto dataFile=args.Option("--data-file");if(data&&dataFile) return fail("choose --data or --data-file");if(action=="set"&&!data&&!dataFile) return fail("csg set needs --data or --data-file");
        if(dataFile) {std::ifstream input(kb::assets::Utf8Path(*dataFile),std::ios::binary);if(!input) return fail("cannot read CSG data file");data=std::string{std::istreambuf_iterator<char>{input},{}};}
        if(data&&!kb::scene::DecodeCsg(*data,source,error)) return fail(error);
        kb::scene::CompiledCsg check;if(!kb::scene::CompileCsg(source,check,error)) return fail(error);
        std::error_code ec;std::filesystem::create_directories(file.parent_path(),ec);if(ec) return fail(ec.message());std::ofstream out(file,std::ios::binary|std::ios::trunc);out<<kb::scene::EncodeCsg(source);out.close();if(!out) return fail("could not save CSG source");io.out<<path<<'\n';return 0;
    }
    if(action=="show") {io.out<<kb::scene::EncodeCsg(source);return 0;}
#if !KB_CLI_RENDERER
    return fail("CSG mesh import requires a renderer-enabled CLI");
#else
    auto requested=args.Option("--output");if(!requested) return fail("csg build needs --output /Game/<folder>");auto folder=kb::assets::ResolveProjectAssetFolder(root,*requested);if(!folder.error.empty()) return fail(folder.error);
    kb::scene::CompiledCsg compiled;if(!kb::scene::CompileCsg(source,compiled,error)) return fail(error);
    // Generated source is kept outside Assets; ordinary import owns the runtime container.
    const auto generated=root/"Saved"/"Csg"/file.stem();std::error_code ec;std::filesystem::create_directories(generated,ec);if(ec) return fail(ec.message());const auto obj=generated/"Geometry.obj";
    if(!kb::scene::WriteCsgObj(obj,compiled,error)) return fail(error);
    kb::scene::Scene scene;if(!MountProjectAssets(scene,root,error)) return fail(error);
    kb::assets::AssetImportOptions options;options.mesh.importTextures=false;options.mesh.importMaterials=false;
    const auto imported=kb::render::RenderMeshImportArtifacts::ImportStaticFiles(scene.Assets().Manager(),std::array{obj},kb::assets::Utf8Path(folder.virtualPath),options);
    if(imported.items.size()!=1||!imported.items.front().Succeeded()) return fail(imported.items.empty()?"CSG mesh import failed":imported.items.front().error);
    if(!kb::assets::WriteCollisionMesh(folder.folder/"Geometry.kbcollision",compiled.mesh,error)) return fail(error);
    io.out<<"mesh: "<<imported.items.front().virtualPath.generic_string()<<"\ncollision: "<<folder.virtualPath<<"/Geometry.kbcollision\nvolume: "<<compiled.volume<<"\ntriangles: "<<compiled.mesh.indices.size()/3<<'\n';return 0;
#endif
}
}
