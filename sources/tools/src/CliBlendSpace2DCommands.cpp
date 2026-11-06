#include "CliCommands.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/BlendSpace2DAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>

namespace kb::cli {
namespace {
using kb::core::JsonValue;
std::string Utf8(const std::filesystem::path& path) {
    const auto value=path.generic_u8string(); return {reinterpret_cast<const char*>(value.data()),value.size()};
}
std::optional<std::array<float,2>> Coordinates(std::string_view text) {
    const auto comma=text.find(',');
    if (comma==std::string_view::npos) return std::nullopt;
    std::array<float,2> values{};
    const std::array words{text.substr(0,comma),text.substr(comma+1)};
    for (std::size_t i=0;i<2;++i) {
        const auto parsed=std::from_chars(words[i].data(),words[i].data()+words[i].size(),values[i]);
        if (parsed.ec!=std::errc{} || parsed.ptr!=words[i].data()+words[i].size() || !std::isfinite(values[i])) return std::nullopt;
    }
    return values;
}
std::optional<std::filesystem::path> AssetFile(std::string text,const std::filesystem::path& root,
    std::string_view extension,std::string& error) {
    auto input=kb::assets::Utf8Path(text);
    if (text.starts_with("/Game/")) input=std::filesystem::path{"Assets"}/kb::assets::Utf8Path(text.substr(6));
    if (!input.is_absolute()) input=root/input;
    const auto resolved=ResolveProjectFile(input,root,error);
    if (resolved.empty()) return std::nullopt;
    std::error_code fileError;
    const auto path=std::filesystem::weakly_canonical(resolved,fileError);
    if (fileError) { error="Asset path could not be resolved";return std::nullopt; }
    const auto content=std::filesystem::weakly_canonical(root/"Assets",fileError);
    const auto relative=path.lexically_relative(content);
    if (fileError || relative.empty() || relative.is_absolute() || *relative.begin()==".." || path.extension()!=extension) {
        error="Expected an asset inside the project's Assets directory with extension "+std::string{extension};return std::nullopt;
    }
    return path;
}
} // namespace

int RunBlendSpace2DCommand(const ArgumentList& arguments,CommandIo io) {
    using namespace kb::scene;
    constexpr std::array<std::string_view,10> allowed{"--project","--space","--at","--clip","--index","--controller","--state","--layer","--x-param","--y-param"};
    const auto project=arguments.Option("--project"),space=arguments.Option("--space");
    const auto fail=[&](const std::string& error) { io.err << "error: " << error << '\n'; return 1; };
    if (!arguments.UnknownOptions(allowed).empty() || !project || !space || arguments.Positionals().size()!=1)
        return fail("blend-space expects new|show|add|set|remove|sample|assign --project <dir> --space <path>.kbblend2d");
    const auto& action=arguments.Positionals()[0];
    constexpr std::array<std::string_view,7> actions{"new","show","add","set","remove","sample","assign"};
    if (std::ranges::find(actions,action)==actions.end())
        return fail("Unknown blend-space operation");
    std::vector<std::string_view> actionOptions{"--project","--space"};
    if (action=="add" || action=="set") actionOptions.insert(actionOptions.end(),{"--at","--clip"});
    if (action=="set" || action=="remove") actionOptions.push_back("--index");
    if (action=="sample") actionOptions.push_back("--at");
    if (action=="assign") actionOptions.insert(actionOptions.end(),{"--controller","--state","--layer","--x-param","--y-param"});
    if (!arguments.UnknownOptions(actionOptions).empty()) return fail("Option is not supported by this blend-space operation");
    Scene scene;
    std::string error;
    auto root=std::filesystem::weakly_canonical(kb::assets::Utf8Path(*project));
    if (root.extension()==".21kbproject") root=root.parent_path();
    if (!MountProjectAssets(scene,root,error)) return fail(error);
    const auto file=AssetFile(*space,root,kBlendSpace2DAssetExtension,error);
    if (!file) return fail(error);
    BlendSpace2DAsset asset;
    if (action=="new") {
        if (std::filesystem::exists(*file)) return fail("BlendSpace2D asset already exists");
    } else {
        const auto loaded=BlendSpace2DAssetIO::Load(*file,error);
        if (!loaded) return fail(error);
        asset=*loaded;
    }
    auto& registry=scene.Assets().Manager().Registry();
    if (action=="add" || action=="set") {
        std::size_t index=asset.samples.size();
        if (action=="set") {
            const auto word=arguments.Option("--index");
            if (!word) return fail("set requires --index <0..255>");
            const auto parsed=std::from_chars(word->data(),word->data()+word->size(),index);
            if (parsed.ec!=std::errc{} || parsed.ptr!=word->data()+word->size() || index>=asset.samples.size()) return fail("Sample index is out of range");
        }
        BlendSpace2DSample sample=action=="set"?asset.samples[index]:BlendSpace2DSample{};
        const auto at=arguments.Option("--at"),clip=arguments.Option("--clip");
        if (action=="add" && (!at || !clip)) return fail("add requires --at x,y and --clip <asset>");
        if (action=="set" && !at && !clip) return fail("set requires --at x,y or --clip <asset>");
        if (at) {
            const auto coordinates=Coordinates(*at);
            if (!coordinates) return fail("--at requires two finite coordinates x,y");
            sample.x=(*coordinates)[0];sample.y=(*coordinates)[1];
        }
        if (clip) {
            kb::assets::AssetId id;
            const auto* metadata=kb::assets::TryParseAssetId(*clip,id) && id.IsValid()
                ? registry.Find(id) : FindAssetByFlexiblePath(registry,kb::assets::Utf8Path(*clip));
            if (!metadata || metadata->type!=kAnimationClipAssetType) return fail("The sample clip is missing or is not an AnimationClip");
            sample.clipReference=kb::assets::ToString(metadata->id);
        }
        if (action=="add") asset.samples.push_back(std::move(sample)); else asset.samples[index]=std::move(sample);
    } else if (action=="remove") {
        std::size_t index=0;
        const auto word=arguments.Option("--index");
        if (!word) return fail("remove requires --index <0..255>");
        const auto parsed=std::from_chars(word->data(),word->data()+word->size(),index);
        if (parsed.ec!=std::errc{} || parsed.ptr!=word->data()+word->size() || index>=asset.samples.size()) return fail("Sample index is out of range");
        asset.samples.erase(asset.samples.begin()+static_cast<std::ptrdiff_t>(index));
    }
    const auto plan=BlendSpace2DPlan::Build(asset,error);
    if (!plan) return fail(error);
    const std::string virtualPath="/Game/"+Utf8(file->lexically_relative(root/"Assets"));
    auto result=JsonValue::MakeObject();
    result.Set("schema",JsonValue::MakeString("21kb.blend-space-2d/v1"));
    result.Set("ok",JsonValue::MakeBool(true));
    result.Set("operation",JsonValue::MakeString(action));
    result.Set("asset",JsonValue::MakeString(virtualPath));
    auto samples=JsonValue::MakeArray();
    for (std::size_t i=0;i<asset.samples.size();++i) {
        const auto& sample=asset.samples[i]; auto row=JsonValue::MakeObject();
        row.Set("index",JsonValue::MakeNumber(static_cast<double>(i)));row.Set("x",JsonValue::MakeNumber(sample.x));row.Set("y",JsonValue::MakeNumber(sample.y));
        row.Set("clip",JsonValue::MakeString(sample.clipReference));samples.Append(std::move(row));
    }
    result.Set("samples",std::move(samples));
    auto triangles=JsonValue::MakeArray();
    for (const auto& triangle:plan->Triangles()) {
        auto row=JsonValue::MakeArray();for (const auto index:triangle) row.Append(JsonValue::MakeNumber(static_cast<double>(index)));
        triangles.Append(std::move(row));
    }
    result.Set("triangles",std::move(triangles));
    if (action=="sample") {
        const auto at=arguments.Option("--at");const auto coordinates=at?Coordinates(*at):std::nullopt;
        if (!coordinates) return fail("sample requires --at x,y");
        const auto weights=plan->Sample((*coordinates)[0],(*coordinates)[1],error);
        if (!weights) return fail(error);
        auto values=JsonValue::MakeArray();
        for (std::size_t i=0;i<weights->count;++i) {
            auto row=JsonValue::MakeObject();row.Set("index",JsonValue::MakeNumber(static_cast<double>(weights->values[i].sample)));
            row.Set("weight",JsonValue::MakeNumber(weights->values[i].weight));values.Append(std::move(row));
        }
        result.Set("weights",std::move(values));
    } else if (action=="assign") {
        const auto controllerWord=arguments.Option("--controller"),stateName=arguments.Option("--state");
        const auto x=arguments.Option("--x-param"),y=arguments.Option("--y-param");
        if (!controllerWord || !stateName || stateName->empty() || !x || !y || x->empty() || y->empty() || *x==*y || asset.samples.empty())
            return fail("assign requires a nonempty space, --controller, --state and distinct --x-param/--y-param names");
        const auto controllerFile=AssetFile(*controllerWord,root,kAnimatorControllerAssetExtension,error);
        if (!controllerFile) return fail(error);
        auto controller=AnimationAssetIO::LoadController(*controllerFile,&error);
        if (!controller) return fail(error);
        const auto layerName=arguments.Option("--layer").value_or(controller->layers.front().name);
        const auto layer=std::ranges::find(controller->layers,layerName,&AnimatorControllerLayer::name);
        if (layer==controller->layers.end()) return fail("Controller layer does not exist");
        const auto state=std::ranges::find(layer->states,*stateName,&AnimatorControllerState::name);
        if (state==layer->states.end()) return fail("Controller state does not exist");
        for (const auto& name:{*x,*y}) {
            const auto parameter=std::ranges::find(controller->parameters,name,&AnimatorParameterDefinition::name);
            if (parameter==controller->parameters.end()) controller->parameters.push_back({.name=name});
            else if (parameter->type!=AnimatorParameterType::Float) return fail("Axis parameter is not a Float");
        }
        state->clipReference.clear();state->blendChildren.clear();state->blendSpaceReference=virtualPath;state->blendParameter=*x;state->blendParameterY=*y;
        if (!AnimationAssetIO::SaveController(*controllerFile,*controller,&error)) return fail(error);
    } else if (action!="show") {
        std::error_code directoryError;std::filesystem::create_directories(file->parent_path(),directoryError);
        if (directoryError || !BlendSpace2DAssetIO::Save(*file,asset,error)) return fail(error.empty()?directoryError.message():error);
    }
    io.out << result.Dump() << '\n';
    return 0;
}
} // namespace kb::cli
