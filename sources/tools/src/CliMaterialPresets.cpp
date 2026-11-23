#include "CliMaterialLibrary.hpp"

#include "CliCommands.hpp"
#include "CliMaterialShared.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/ScenePrefabPrivateScene.hpp"
#include "engine/math/ColorSpace.hpp"
#include "engine/script/ScriptAgentSearch.hpp"
#include "engine/script/ScriptNameSuggestion.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphCost.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialGraphNodeDocs.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <sstream>

namespace kb::cli::material {
namespace {

constexpr std::string_view kLibraryFolder = "Assets/Materials/Library";

[[nodiscard]] std::string PascalCase(std::string_view name) {
    std::string out;
    bool upper = true;
    for (const char character : name) {
        if (character == '-' || character == '_' || character == ' ') {
            upper = true;
            continue;
        }
        out.push_back(upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(character))) : character);
        upper = false;
    }
    return out;
}

[[nodiscard]] std::string JoinWords(const std::vector<std::string>& words) {
    std::string joined;
    for (const std::string& word : words) joined += (joined.empty() ? "" : ", ") + word;
    return joined;
}

// The base colour (linear) of the material an object wears, and whether that material also has a colour texture.
struct ObjectLook {
    std::optional<std::array<float, 3>> color;
    bool texture = false;
};

[[nodiscard]] ObjectLook LookOfObject(const ArgumentList& arguments, const std::filesystem::path& projectRoot, const std::string& entity) {
    std::vector<std::string> show{ "show", "--project", arguments.Option("--project").value_or(std::string{}), "--name", entity };
    if (const std::optional<std::string> scene = arguments.Option("--scene")) show.insert(show.end(), { "--scene", *scene });
    std::ostringstream out;
    std::ostringstream err;
    if (RunEntityCommand(ArgumentList{ show }, CommandIo{ .out = out, .err = err }) != 0) return {};
    const std::string text = out.str();
    const std::size_t at = text.find("material=/Game/");
    if (at == std::string::npos) return {};
    const std::string path = text.substr(at + 15U, text.find_first_of(" \r\n", at) - at - 15U);
    const std::filesystem::path file = projectRoot / "Assets" / path;
    ObjectLook look;
    if (file.extension() == ".kbmatinst") {
        if (const auto instance = kb::render::RenderMaterialInstanceAssetLoader::LoadInstance(file)) {
            for (const kb::render::RenderMaterialGraphParameterValue& value : instance->overrides.graphParameterValues) {
                if (Normalized(value.stableId) == "color") look.color = std::array<float, 3>{ value.numbers[0], value.numbers[1], value.numbers[2] };
            }
        }
    } else if (const auto material = kb::render::RenderMaterialAssetLoader::LoadMaterial(file)) {
        look.color = std::array<float, 3>{ material->desc.baseColor[0], material->desc.baseColor[1], material->desc.baseColor[2] };
        look.texture = material->desc.albedoTextureAssetId != 0U;
    }
    return look;
}

// HeightMin and HeightMax of a layered terrain material: the lowest and the highest ground of the terrain the material goes on, in
// world height (the terrain's own heights, scaled and lifted by its object), so the top layer is reached at the top of the ground.
[[nodiscard]] std::vector<std::string> TerrainHeightSettings(const ArgumentList& arguments, const std::string& entity) {
    const std::string project = arguments.Option("--project").value_or(std::string{});
    std::vector<std::string> show{ "show", "--project", project, "--name", entity };
    if (const std::optional<std::string> scene = arguments.Option("--scene")) show.insert(show.end(), { "--scene", *scene });
    std::ostringstream shown;
    std::ostringstream ignored;
    if (RunEntityCommand(ArgumentList{ show }, CommandIo{ .out = shown, .err = ignored }) != 0) return {};
    const std::string text = shown.str();
    const std::size_t meshAt = text.find("mesh=/Game/");
    if (meshAt == std::string::npos) return {};
    const std::string mesh = text.substr(meshAt + 5U, text.find_first_of(" \r\n", meshAt) - meshAt - 5U);
    if (!mesh.ends_with(".kbterrain")) return {};
    const auto number = [&](const std::string& source, const std::string& key, int index, float fallback) {
        const std::size_t at = source.find(key);
        if (at == std::string::npos) return fallback;
        const char* cursor = source.c_str() + at + key.size();
        for (int skipped = 0; skipped < index && cursor != nullptr; ++skipped) cursor = std::strchr(cursor, ',') != nullptr ? std::strchr(cursor, ',') + 1 : nullptr;
        return cursor != nullptr ? static_cast<float>(std::atof(cursor)) : fallback;
    };
    const float lift = number(text, "worldPosition=", 1, 0.0F);
    const float scale = number(text, "localScale=", 1, 1.0F);
    std::ostringstream info;
    if (RunTerrainCommand(ArgumentList{ std::vector<std::string>{ "info", "--project", project, "--terrain", mesh } }, CommandIo{ .out = info, .err = ignored }) != 0) return {};
    const std::string infoText = info.str();
    const std::size_t at = infoText.find("height min ");
    if (at == std::string::npos) return {};
    const float low = static_cast<float>(std::atof(infoText.c_str() + at + 11U));
    const std::size_t highAt = infoText.find(" max ", at);
    if (highAt == std::string::npos) return {};
    const float high = static_cast<float>(std::atof(infoText.c_str() + highAt + 5U));
    char heights[2][48];
    std::snprintf(heights[0], sizeof(heights[0]), "HeightMin=%.3f", lift + low * scale);
    std::snprintf(heights[1], sizeof(heights[1]), "HeightMax=%.3f", lift + high * scale);
    return { heights[0], heights[1] };
}

// "Color=@object[*factor]:#fallback" in a preset: the colour the object already wears (darker by the factor), else the fallback.
[[nodiscard]] std::string ObjectColorSetting(const std::string& setting, const ObjectLook& look) {
    const std::size_t equals = setting.find('=');
    const std::string value = setting.substr(equals + 1U);
    if (!value.starts_with("@object")) return setting;
    const std::size_t colon = value.find(':');
    const std::string fallback = colon == std::string::npos ? std::string{} : value.substr(colon + 1U);
    if (!look.color.has_value()) return setting.substr(0U, equals + 1U) + fallback;
    const float factor = value.size() > 7U && value[7] == '*' ? static_cast<float>(std::atof(value.c_str() + 8)) : 1.0F;
    char hex[16];
    const auto byte = [&](float linear) { return static_cast<unsigned>(std::clamp(kb::math::LinearToSrgb(std::clamp(linear * factor, 0.0F, 1.0F)) * 255.0F + 0.5F, 0.0F, 255.0F)); };
    std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", byte((*look.color)[0]), byte((*look.color)[1]), byte((*look.color)[2]));
    return setting.substr(0U, equals + 1U) + hex;
}

} // namespace

const MaterialMaster* FindMaterialMaster(std::string_view name) noexcept {
    const auto found = std::ranges::find(MaterialMasters(), name, &MaterialMaster::name);
    return found != MaterialMasters().end() ? &*found : nullptr;
}

const MaterialPreset* FindMaterialPreset(std::string_view name) noexcept {
    const auto found = std::ranges::find_if(MaterialPresets(), [&](const MaterialPreset& preset) { return Normalized(preset.name) == Normalized(name); });
    return found != MaterialPresets().end() ? &*found : nullptr;
}

std::vector<std::string> PresetSettings(const MaterialPreset& preset) {
    std::vector<std::string> settings;
    std::string_view rest = preset.values;
    while (!rest.empty()) {
        const std::size_t bar = rest.find('|');
        settings.emplace_back(rest.substr(0U, bar));
        rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1U);
    }
    return settings;
}

std::vector<std::string> MasterParameters(const MaterialMaster& master) {
    std::vector<std::string> names;
    std::istringstream lines{ std::string{ master.text } };
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t equals = line.find(" = Parameter");
        if (equals != std::string::npos) names.push_back(line.substr(0U, equals));
    }
    return names;
}

std::string MaterialPresetIndex() {
    std::vector<std::string_view> families;
    for (const MaterialPreset& preset : MaterialPresets()) {
        if (std::ranges::find(families, preset.family) == families.end()) families.push_back(preset.family);
    }
    std::string text = "# Ready materials (" + std::to_string(MaterialPresets().size()) + "): material-new --preset <name>, parameters with --set\n";
    for (const std::string_view family : families) {
        std::string names;
        for (const MaterialPreset& preset : MaterialPresets()) {
            if (preset.family == family) names += (names.empty() ? "" : ", ") + std::string{ preset.name };
        }
        text += std::string{ family } + ": " + names + "\n";
    }
    return text;
}

std::vector<LibraryHit> SearchMaterialLibrary(std::string_view words) {
    const std::vector<std::string> query = kb::script::SearchWords(words);
    std::vector<LibraryHit> hits;
    if (query.empty()) return hits;
    for (const MaterialPreset& preset : MaterialPresets()) {
        std::vector<std::string> other = kb::script::SearchWords(preset.tags);
        for (std::string& word : kb::script::SearchWords(preset.family)) other.push_back(std::move(word));
        const std::vector<std::string> name = kb::script::SearchWords(preset.name);
        const int score = kb::script::SearchScore(query, name, other);
        if (score == 0) continue;
        std::vector<std::string> covered;
        for (const std::string& word : query) {
            if (kb::script::SearchWordScore(word, name, other) >= 2) covered.push_back(word);
        }
        const MaterialMaster* master = FindMaterialMaster(preset.master);
        const std::vector<std::string> parameters = master != nullptr ? MasterParameters(*master) : std::vector<std::string>{};
        hits.push_back({ score, "preset", std::string{ preset.family },
            "preset " + std::string{ preset.name } + " (" + std::string{ preset.family } + "): material-new --preset " + std::string{ preset.name } +
                " [--name <Name>] [--set Parameter=value ...] [--entity <Object>]; parameters: " + JoinWords(parameters), std::move(covered) });
    }
    for (const kb::render::RenderMaterialGraphNodeDoc& doc : kb::render::RenderMaterialGraphNodeDocs()) {
        const std::string kind{ kb::render::RenderMaterialGraphNodeKindName(doc.kind) };
        std::vector<std::string> other = kb::script::SearchWords(doc.purpose);
        for (std::string& word : kb::script::SearchWords(doc.group)) other.push_back(std::move(word));
        // A word in a node's description is weaker evidence than one in a ready material's tags: half the score.
        const std::vector<std::string> name = kb::script::SearchWords(kind, true);
        const int score = kb::script::SearchScore(query, name, other) / 2;
        if (score == 0) continue;
        std::vector<std::string> covered;
        for (const std::string& word : query) {
            if (kb::script::SearchWordScore(word, name, other) >= 2) covered.push_back(word);
        }
        hits.push_back({ score, "node", "node", "node " + kind + ": material-graph node --kind " + kind + " - " + std::string{ doc.purpose }, std::move(covered) });
    }
    std::ranges::stable_sort(hits, [](const LibraryHit& left, const LibraryHit& right) { return left.score > right.score; });
    return hits;
}

namespace {

// The master in the project's library folder: written from its graph text the first time a preset needs it.
[[nodiscard]] bool EnsureMaster(const std::filesystem::path& projectRoot, const MaterialMaster& master, CommandIo io) {
    const std::filesystem::path file = projectRoot / kLibraryFolder / (std::string{ master.name } + ".kbmat");
    std::error_code error;
    if (std::filesystem::exists(file, error)) return true;
    kb::scene::Scene scene;
    std::string mountError;
    if (!MountProjectAssets(scene, projectRoot, mountError)) {
        io.err << "error: " << mountError << '\n';
        return false;
    }
    const kb::assets::AssetRegistry& registry = scene.Assets().Manager().Registry();
    kb::render::RenderMaterialAssetData asset{};
    asset.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    if (const std::string problem = ApplyGraphText(asset, master.text, ProjectTextureResolver(registry), registry); !problem.empty()) {
        io.err << "error: the library material " << master.name << " could not be made: " << problem << '\n';
        return false;
    }
    std::filesystem::create_directories(file.parent_path(), error);
    if (!kb::render::RenderMaterialAssetWriter::Save(file, asset)) {
        io.err << "error: the library material could not be written: " << Utf8Text(file) << '\n';
        return false;
    }
    return true;
}

// The material on the Mesh Renderers of a prefab asset (the ones of the part --entity names, else all), saved to the prefab.
[[nodiscard]] bool ApplyToPrefab(const std::filesystem::path& projectRoot, const std::string& prefabText, const std::optional<std::string>& part,
    const std::string& materialPath, CommandIo io) {
    kb::scene::Scene scene;
    std::string error;
    if (!MountProjectAssets(scene, projectRoot, error)) {
        io.err << "error: " << error << '\n';
        return false;
    }
    const kb::assets::AssetRegistry& registry = scene.Assets().Manager().Registry();
    const kb::assets::AssetMetadata* prefabAsset = FindAssetByFlexiblePath(registry, kb::assets::Utf8Path(prefabText));
    const kb::assets::AssetMetadata* materialAsset = FindAssetByFlexiblePath(registry, kb::assets::Utf8Path(materialPath));
    if (prefabAsset == nullptr || prefabAsset->physicalPath.extension() != ".kbprefab") {
        io.err << "error: no prefab " << prefabText << " in the project\n";
        return false;
    }
    if (materialAsset == nullptr) {
        io.err << "error: the new material " << materialPath << " is not in the project's assets\n";
        return false;
    }
    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Load(prefabAsset->physicalPath);
    kb::scene::ScenePrefabPrivateScene edit = handle.IsValid() ? scene.Prefabs().OpenPrivateScene(handle) : kb::scene::ScenePrefabPrivateScene{};
    if (!edit.IsValid()) {
        io.err << "error: " << prefabText << " could not be opened\n";
        return false;
    }
    static_cast<void>(edit.EditScene().Assets().MountProject(projectRoot));
    // Without a part the material goes on every Mesh Renderer, so a prefab whose parts wear different materials is refused: the others would be lost.
    if (!part.has_value()) {
        std::set<std::uint64_t> materials;
        std::string parts;
        for (std::uint32_t index = 0U; index < edit.ObjectCount(); ++index) {
            const kb::scene::SceneEntity entity = edit.ObjectAt(index).Entity();
            if (const kb::scene::MeshRendererComponent* renderer = edit.EditScene().Components().MeshRenderers().TryGet(entity)) {
                materials.insert(renderer->materialAssetId);
                parts += (parts.empty() ? "" : ", ") + std::string{ edit.EditScene().Entities().Name(entity) };
            }
        }
        if (materials.size() > 1U) {
            io.err << "error: " << prefabText << " has parts with different materials (" << parts << "); name the part with --entity <Part> so the others keep theirs\n";
            return false;
        }
    }
    std::size_t changed = 0U;
    for (std::uint32_t index = 0U; index < edit.ObjectCount(); ++index) {
        const kb::scene::SceneEntity entity = edit.ObjectAt(index).Entity();
        if (part.has_value() && edit.EditScene().Entities().Name(entity) != *part) continue;
        if (const kb::scene::MeshRendererComponent* renderer = edit.EditScene().Components().MeshRenderers().TryGet(entity)) {
            kb::scene::MeshRendererComponent updated = *renderer;
            updated.materialAssetId = materialAsset->id.value;
            edit.EditScene().Components().MeshRenderers().Set(entity, updated);
            ++changed;
        }
    }
    if (changed == 0U) {
        io.err << "error: " << prefabText << (part.has_value() ? " has no part '" + *part + "' with a Mesh Renderer" : " has no Mesh Renderer") << '\n';
        return false;
    }
    if (!edit.Apply() || !scene.Prefabs().Save(edit.SourcePrefab(), prefabAsset->physicalPath)) {
        io.err << "error: " << prefabText << " could not be saved\n";
        return false;
    }
    io.out << "put " << materialPath << " on " << changed << " Mesh Renderer" << (changed == 1U ? "" : "s") << " of " << prefabText << '\n';
    return true;
}

} // namespace

int RunMaterialPresetNew(const ArgumentList& arguments, CommandIo io) {
    const std::string presetName = arguments.Option("--preset").value_or(std::string{});
    const MaterialPreset* preset = FindMaterialPreset(presetName);
    if (preset == nullptr) {
        std::vector<std::string_view> names;
        for (const MaterialPreset& candidate : MaterialPresets()) names.push_back(candidate.name);
        // The nearest name however far: the person typed a look, so one of the library is what was meant.
        const std::string closest = kb::script::NearestScriptName(presetName, std::span<const std::string_view>{ names });
        io.err << "error: there is no material preset '" << presetName << '\'' << (closest.empty() ? std::string{} : "; did you mean " + closest + "?")
               << " (guide with search \"<words>\" finds one; " << names.size() << " presets)\n";
        return 1;
    }
    const std::filesystem::path projectRoot = ProjectRoot(arguments, "material-new", io);
    if (projectRoot.empty()) return 1;
    const MaterialMaster* master = FindMaterialMaster(preset->master);
    if (master == nullptr || !EnsureMaster(projectRoot, *master, io)) return 1;

    const std::string name = arguments.Option("--name").value_or(PascalCase(preset->name));
    if (const std::string problem = kb::assets::AssetFileNameError(name); !problem.empty()) {
        io.err << "error: --name '" << name << "': " << problem << '\n';
        return 1;
    }
    const std::string masterPath = "/Game/Materials/Library/" + std::string{ master->name } + ".kbmat";
    const std::string projectText = arguments.Option("--project").value_or(std::string{});
    const std::string folder = arguments.Option("--folder").value_or("Assets/Materials");
    const std::string virtualFolder = "/Game/" + (folder.starts_with("Assets/") ? folder.substr(7U) : std::string{ "Materials" });
    std::vector<std::string> settings = PresetSettings(*preset);
    for (const std::string& setting : arguments.Options("--set")) settings.push_back(setting);
    // A look laid over an object keeps the colour the object has; a colour the person named with --set wins.
    const std::optional<std::string> object = arguments.Option("--entity");
    const bool onObject = object.has_value() && !arguments.Option("--prefab").has_value();
    const ObjectLook look = onObject ? LookOfObject(arguments, projectRoot, *object) : ObjectLook{};
    std::vector<std::string> named;
    for (const std::string& setting : arguments.Options("--set")) named.push_back(setting.substr(0U, setting.find('=')));
    // A terrain material takes its height range from the terrain it goes on, unless the call names one.
    if (onObject) {
        const std::vector<std::string> parameters = MasterParameters(*master);
        const auto has = [&](const std::string& key) { return std::ranges::find(parameters, key) != parameters.end(); };
        if (has("HeightMin") && has("HeightMax") && std::ranges::find(named, "HeightMin") == named.end() && std::ranges::find(named, "HeightMax") == named.end()) {
            for (const std::string& setting : TerrainHeightSettings(arguments, *object)) {
                settings.push_back(setting);
                io.out << "Note: " << setting << " (the height of the terrain under " << *object << ")\n";
            }
        }
    }
    std::erase_if(settings, [&](const std::string& setting) {
        return setting.find("=@object") != std::string::npos && std::ranges::find(named, setting.substr(0U, setting.find('='))) != named.end();
    });
    for (std::string& setting : settings) setting = ObjectColorSetting(setting, look);
    const kb::render::RenderMaterialAssetData parent = kb::render::RenderMaterialAssetLoader::LoadMaterial(projectRoot / kLibraryFolder / (std::string{ master->name } + ".kbmat"))
        .value_or(kb::render::RenderMaterialAssetData{});

    std::string virtualPath;
    std::ostringstream made;
    // A Surface material is an instance of its master. The Post Process and User Interface passes draw only a material of its own, so
    // those presets are the master's graph with the preset's values set in it.
    const bool instance = kb::render::ParseRenderMaterialDomain(parent.graph.materialDomain) == kb::render::RenderMaterialDomain::Surface;
    if (instance) {
        std::vector<std::string> words{ "--project", projectText, "--parent", masterPath, "--name", name, "--folder", folder };
        for (const std::string& setting : settings) words.insert(words.end(), { "--set", setting });
        if (RunMaterialInstanceNewCommand(ArgumentList{ words }, CommandIo{ .out = made, .err = io.err }) != 0) return 1;
        virtualPath = virtualFolder + "/" + name + ".kbmatinst";
    } else {
        std::ostringstream quiet;
        const std::vector<std::string> words{ "--project", projectText, "--name", name, "--folder", folder, "--graph", std::string{ master->text } };
        if (RunMaterialNewCommand(ArgumentList{ words }, CommandIo{ .out = quiet, .err = io.err }) != 0) return 1;
        virtualPath = virtualFolder + "/" + name + ".kbmat";
        for (const std::string& setting : settings) {
            const auto [parameter, value] = SplitSet(setting);
            const std::vector<std::string> set{ "set", "--project", projectText, "--material", virtualPath, "--node", parameter, "--value", value };
            std::ostringstream setOut;
            if (RunMaterialGraphCommand(ArgumentList{ set }, CommandIo{ .out = setOut, .err = io.err }) != 0) {
                io.err << "error: the material " << virtualPath << " was made, but '" << setting << "' could not be set (parameters: " << JoinWords(MasterParameters(*master)) << ")\n";
                return 1;
            }
            made << "  " << parameter << " = " << value << '\n';
        }
    }
    io.out << "created " << virtualPath << " from the preset " << preset->name << (instance ? " (master " + masterPath + ")" : std::string{ " (a material of its own)" }) << '\n' << made.str();
    const kb::render::RenderMaterialGraphCost cost = kb::render::EstimateRenderMaterialGraphCost(parent.graph);
    io.out << "Cost: about " << cost.instructions << " instructions, " << cost.textureSamples << " texture samples\n";
    for (const std::string& warning : cost.mobileWarnings) io.out << "Warning: too heavy for a phone: " << warning << '\n';
    for (const std::string& note : cost.notes) io.out << "Note: " << note << '\n';

    if (look.texture && preset->values.find("=@object") != std::string_view::npos) {
        io.out << "Note: " << *object << "'s material has a colour texture that this look does not carry; to keep it, add a TextureParameter node to the new material and connect it to its Color input (material-graph)\n";
    }
    if (const std::optional<std::string> entity = arguments.Option("--entity"); entity.has_value() && !arguments.Option("--prefab").has_value()) {
        std::vector<std::string> set{ "set", "--project", arguments.Option("--project").value_or(std::string{}), "--entity", *entity, "--type", "MeshRenderer",
            "--set", "material=" + virtualPath };
        if (const std::optional<std::string> scene = arguments.Option("--scene")) set.insert(set.end(), { "--scene", *scene });
        if (RunComponentCommand(ArgumentList{ set }, io) != 0) {
            io.err << "error: the material " << virtualPath << " was made, but it could not be put on " << *entity << '\n';
            return 1;
        }
    }
    if (const std::optional<std::string> prefab = arguments.Option("--prefab"); prefab.has_value()) {
        if (!ApplyToPrefab(projectRoot, *prefab, arguments.Option("--entity"), virtualPath, io)) {
            io.err << "error: the material " << virtualPath << " was made, but it could not be put on the prefab\n";
            return 1;
        }
    }
    return 0;
}

} // namespace kb::cli::material
