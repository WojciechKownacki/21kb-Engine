#include "CliCommands.hpp"
#include "CliMaterialShared.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Rig2DAsset.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include <fstream>
#include <iterator>
namespace kb::cli {
namespace {
kb::scene::Rig2DAsset Sample() {
    using namespace kb::scene;Rig2DAsset a;a.duration=2;a.looping=false;
    a.bones={{"Root",-1,{}},{"Tip",0,{.position={0,1}}}};
    for(int row=0;row<3;++row) for(int column=0;column<2;++column) {
        Rig2DVertex v;v.position={column? .4F:-.4F,static_cast<float>(row)};v.uv={static_cast<float>(column),1-static_cast<float>(row)*.5F};v.bones={0,1,0,0};v.weights={1-static_cast<float>(row)*.5F,static_cast<float>(row)*.5F,0,0};a.vertices.push_back(v);
    }
    a.indices={0,2,1,1,2,3,2,4,3,3,4,5};a.tracks={{1,{{0,{.position={0,1}}},{2,{.position={.5F,1},.rotationDegrees=70}}}}};return a;
}
}
int RunRig2DCommand(const ArgumentList& args,CommandIo io) {
    const auto root=material::ProjectRoot(args,"rig2d",io);if(root.empty()) return 1;
    const auto fail=[&](const std::string& error){io.err<<"error: "<<error<<'\n';return 1;};
    const auto& words=args.Positionals();const std::string action=words.empty()?"":words.front();std::string error;std::filesystem::path file;std::string virtualPath;kb::scene::Rig2DAsset rig;
    if(action=="new") {
        const auto name=args.Option("--name");if(!name) return fail("rig2d new needs --name");error=kb::assets::AssetFileNameError(*name);if(!error.empty()) return fail(error);
        const auto folder=kb::assets::ResolveProjectAssetFolder(root,args.Option("--folder").value_or("/Game/Rigs"));if(!folder.error.empty()) return fail(folder.error);
        file=folder.folder/kb::assets::Utf8Path(*name+".kbrig2d");virtualPath=folder.virtualPath+"/"+*name+".kbrig2d";
        if(std::filesystem::exists(file)) return fail("rig2d source already exists");rig=Sample();
    } else if(action=="show"||action=="set"||action=="build") {
        const auto path=args.Option("--rig");if(!path) return fail("rig2d show/set/build needs --rig /Game/<path>.kbrig2d");
        const auto located=material::ProjectAssetFile(root,*path,".kbrig2d","rig2d source","create one with rig2d new",io);if(!located) return 1;
        file=located->file;virtualPath=located->virtualPath;std::ifstream input(file,std::ios::binary);const std::string text{std::istreambuf_iterator<char>{input},{}};
        if(!input||!kb::scene::DecodeRig2D(text,rig,error)) return fail("could not read rig2d source: "+error);
    } else return fail("rig2d expects new, show, set or build");
    if(action=="new"||action=="set") {
        const auto json=args.Option("--data");if(action=="set"&&!json) return fail("rig2d set needs --data <complete source object>");
        if(json&&!kb::scene::DecodeRig2D(*json,rig,error)) return fail(error);
        std::error_code ec;std::filesystem::create_directories(file.parent_path(),ec);if(ec||!kb::scene::SaveRig2D(file,rig,error)) return fail(ec?ec.message():error);
    }
    if(action!="build") {io.out<<kb::scene::EncodeRig2D(rig);return 0;}
    const std::string folderPath=virtualPath.substr(0,virtualPath.size()-std::string_view{".kbrig2d"}.size());
    const auto folder=kb::assets::ResolveProjectAssetFolder(root,folderPath);if(!folder.error.empty()) return fail(folder.error);
    const auto skeletonPath=folder.virtualPath+"/Skeleton.kbskeleton",clipPath=folder.virtualPath+"/Motion.kbanim";
    const auto id=kb::assets::MakeAssetId(kb::assets::NormalizeAssetPath(kb::assets::Utf8Path(skeletonPath))+":Skeleton");
    kb::scene::CompiledRig2D compiled;if(!kb::scene::CompileRig2D(rig,id.value,0,clipPath,compiled,error)||!kb::scene::AnimationAssetIO::ValidateController(compiled.controller,&error)) return fail(error);
    std::error_code ec;std::filesystem::create_directories(folder.folder,ec);if(ec) return fail(ec.message());
    if(!kb::scene::SkeletonAssetIO::Save(folder.folder/"Skeleton.kbskeleton",compiled.skeleton)||
       !kb::scene::SkeletalMeshAssetIO::Save(folder.folder/"Mesh.kbskeletalmesh",compiled.mesh)||
       !kb::scene::AnimationAssetIO::SaveClip(folder.folder/"Motion.kbanim",compiled.clip)||
       !kb::scene::AnimationAssetIO::SaveController(folder.folder/"Controller.kbanimcontroller",compiled.controller,&error)) return fail("could not save compiled rig2d assets: "+error);
    io.out<<"mesh: "<<folder.virtualPath<<"/Mesh.kbskeletalmesh\ncontroller: "<<folder.virtualPath<<"/Controller.kbanimcontroller\n";return 0;
}
}
