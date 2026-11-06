#include "CliCommands.hpp"
#include "CliMaterialShared.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/scene/VectorShape2DAsset.hpp"
#include <fstream>
#include <iterator>

namespace kb::cli {
namespace {
int Run(std::string_view command,const ArgumentList& arguments,CommandIo io) {
    const auto root=material::ProjectRoot(arguments,command,io);if(root.empty()) return 1;
    const auto fail=[&](const std::string& error){io.err<<"error: "<<error<<'\n';return 1;};
    kb::scene::VectorShape2DAsset shape;std::filesystem::path file;
    std::string error;
    if(command=="vector2d-new") {
        const auto name=arguments.Option("--name");if(!name) return fail("vector2d-new needs --name");
        error=kb::assets::AssetFileNameError(*name);if(!error.empty()) return fail(error);
        const auto folder=kb::assets::ResolveProjectAssetFolder(root,arguments.Option("--folder").value_or("/Game/Shapes"));
        if(!folder.error.empty()) return fail(folder.error);
        file=folder.folder/kb::assets::Utf8Path(*name+".kbvector2d");if(std::filesystem::exists(file)) return fail("vector shape already exists");
        const auto kind=arguments.Option("--kind").value_or("Polygon");if(kind!="Polygon"&&kind!="Line") return fail("--kind takes Polygon or Line");
        if(kind=="Line") shape.kind=kb::scene::VectorShape2DKind::Line;
    } else {
        const auto asset=arguments.Option("--shape");if(!asset) return fail("vector2d-show/set needs --shape /Game/<path>.kbvector2d");
        const auto located=material::ProjectAssetFile(root,*asset,".kbvector2d","vector shape","create one with vector2d-new",io);if(!located) return 1;
        file=located->file;std::ifstream input(file,std::ios::binary);const std::string text{std::istreambuf_iterator<char>{input},{}};
        if(!input||!kb::scene::DecodeVectorShape2D(text,shape,error)) return fail("could not read vector shape: "+error);
    }
    if(command!="vector2d-show") {
        kb::core::JsonValue data;
        if(!kb::core::JsonValue::Parse(kb::scene::EncodeVectorShape2D(shape),data,error)) return fail(error);
        for(const auto* field:{"points","width","closed","material"}) {
            const auto text=arguments.Option(std::string{"--"}+field);if(!text) continue;
            kb::core::JsonValue value;
            if(std::string_view{field}=="material") value=kb::core::JsonValue::MakeString(*text);
            else if(!kb::core::JsonValue::Parse(*text,value,error)) return fail(error);
            data.Set(field,std::move(value));
        }
        if(!kb::scene::DecodeVectorShape2D(data.Dump(),shape,error)) return fail(error);
        std::error_code directoryError;std::filesystem::create_directories(file.parent_path(),directoryError);
        if(directoryError) return fail(directoryError.message());
        if(!kb::scene::SaveVectorShape2D(file,shape,error)) return fail(error);
    }
    io.out<<kb::scene::EncodeVectorShape2D(shape);return 0;
}
}
int RunVectorShapeNewCommand(const ArgumentList& args,CommandIo io){return Run("vector2d-new",args,io);}
int RunVectorShapeShowCommand(const ArgumentList& args,CommandIo io){return Run("vector2d-show",args,io);}
int RunVectorShapeSetCommand(const ArgumentList& args,CommandIo io){return Run("vector2d-set",args,io);}
}
