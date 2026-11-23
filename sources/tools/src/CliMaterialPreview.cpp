#include "CliCaptureImage.hpp"
#include "CliCommands.hpp"
#include "CliMaterialShared.hpp"
#include "CliProgram.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphCost.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

// material-preview: one or more materials drawn on spheres, headless, as a picture for a model to look at. The spheres stand
// in a temporary scene of the project (removed again) lit by one sun, seen by an orthographic camera, and the player
// renders it as `capture` does. A Post Process material is laid over a view of spheres, a User Interface material fills
// an image over them.

namespace kb::cli {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kSceneName = "MaterialPreviewTemporary";

[[nodiscard]] std::string Number(double value) {
    return FormatCliNumber(value);
}

struct Previewed {
    std::string path;
    std::string domain;
    std::string problem;
};

// The domain of a material or of the parent of an instance, as its graph says.
[[nodiscard]] Previewed Inspect(const fs::path& projectRoot, const std::string& text, CommandIo io) {
    Previewed previewed;
    const std::optional<material::ProjectAsset> asset = [&]() -> std::optional<material::ProjectAsset> {
        std::ostringstream quiet;
        const bool instance = text.ends_with(".kbmatinst");
        return material::ProjectAssetFile(projectRoot, text, instance ? ".kbmatinst" : ".kbmat", "material",
            "make one with material-new, or a ready one with material-new --preset <name>", CommandIo{ .out = quiet, .err = io.err });
    }();
    if (!asset.has_value()) {
        previewed.problem = "missing";
        return previewed;
    }
    previewed.path = asset->virtualPath;
    fs::path graphFile = asset->file;
    if (asset->file.extension() == ".kbmatinst") {
        // An instance draws its parent's graph; the parent is a .kbmat of the project named by its id.
        kb::scene::Scene scene;
        std::string error;
        const std::optional<kb::render::RenderMaterialInstanceAssetData> instance = kb::render::RenderMaterialInstanceAssetLoader::LoadInstance(asset->file);
        if (!instance.has_value() || !MountProjectAssets(scene, projectRoot, error)) {
            previewed.problem = "the instance could not be read";
            return previewed;
        }
        const kb::assets::AssetMetadata* parent = scene.Assets().Manager().Registry().Find(instance->parentMaterialAssetId);
        if (parent == nullptr) {
            previewed.problem = "the parent of the instance is missing";
            return previewed;
        }
        graphFile = parent->physicalPath;
    }
    const std::optional<kb::render::RenderMaterialAssetData> data = kb::render::RenderMaterialAssetLoader::LoadMaterial(graphFile);
    if (!data.has_value()) {
        previewed.problem = "the material could not be read";
        return previewed;
    }
    previewed.domain = std::string{ kb::render::RenderMaterialDomainName(kb::render::ParseRenderMaterialDomain(data->graph.materialDomain)) };
    return previewed;
}

// Runs one command quietly; its output is kept for the error message.
class Steps {
public:
    explicit Steps(const std::string& project) : project_(project) {}

    [[nodiscard]] bool Run(int (*command)(const ArgumentList&, CommandIo), std::vector<std::string> words) {
        // After the action word (entity add ...), or first when the command has none (scene-new --name ...).
        words.insert(words.begin() + (words.front().starts_with("--") ? 0 : 1), { "--project", project_ });
        std::ostringstream out;
        std::ostringstream err;
        const bool ok = command(ArgumentList{ words }, CommandIo{ .out = out, .err = err }) == 0;
        if (!ok && error_.empty()) error_ = err.str() + out.str();
        return ok;
    }
    [[nodiscard]] const std::string& Error() const { return error_; }

private:
    std::string project_;
    std::string error_;
};

} // namespace

int RunMaterialPreviewCommand(const ArgumentList& arguments, CommandIo io) {
    const fs::path projectRoot = material::ProjectRoot(arguments, "material-preview", io);
    if (projectRoot.empty()) return 1;
    const std::vector<std::string> materials = arguments.Options("--material");
    if (materials.empty() || materials.size() > 400U) {
        io.err << "error: material-preview requires --material /Game/<path>.kbmat (or .kbmatinst), once for each material, up to 400 (they are drawn side by side)\n";
        return 1;
    }
    std::vector<Previewed> shown;
    for (const std::string& text : materials) {
        Previewed previewed = Inspect(projectRoot, text, io);
        if (!previewed.problem.empty()) {
            if (previewed.problem != "missing") io.err << "error: " << text << ": " << previewed.problem << '\n';
            return 1;
        }
        shown.push_back(std::move(previewed));
    }
    const bool post = std::ranges::any_of(shown, [](const Previewed& item) { return item.domain == "postProcess"; });
    const bool ui = std::ranges::any_of(shown, [](const Previewed& item) { return item.domain == "userInterface"; });
    if ((post || ui) && shown.size() != 1U) {
        io.err << "error: a Post Process or User Interface material is previewed alone: it covers the whole view or an image of it\n";
        return 1;
    }
    for (const Previewed& item : shown) {
        if (item.domain != "surface" && item.domain != "postProcess" && item.domain != "userInterface") {
            io.err << "error: " << item.path << " is a " << item.domain << " material; previews draw Surface, Post Process and User Interface materials\n";
            return 1;
        }
    }

    // A grid of spheres 2.2 apart; one material fills the picture.
    const std::size_t count = post || ui ? 3U : shown.size();
    const std::size_t columns = post || ui ? 3U : count == 1U ? 1U : static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    const std::size_t rows = (count + columns - 1U) / columns;
    const unsigned cell = count == 1U ? 480U : (count <= 12U ? 192U : 96U);
    const unsigned width = static_cast<unsigned>(columns) * cell;
    const unsigned height = static_cast<unsigned>(rows) * cell;
    constexpr double kSpacing = 2.2;

    std::optional<std::string> out = arguments.Option("--out");
    const fs::path picture = out.has_value() ? fs::absolute(fs::path{ kb::assets::Utf8Path(*out) }.is_relative() ? projectRoot / kb::assets::Utf8Path(*out) : kb::assets::Utf8Path(*out))
                                             : NextCapturePath(projectRoot);
    const std::string sceneFile = "Assets/Scenes/" + std::string{ kSceneName } + ".21kbscene";
    const std::string scenePath = "/Game/Scenes/" + std::string{ kSceneName } + ".21kbscene";
    const fs::path sceneDisk = projectRoot / sceneFile;
    std::error_code status;
    if (fs::exists(sceneDisk, status)) {
        io.err << "error: " << sceneFile << " is left over from an earlier preview; remove it with asset delete\n";
        return 1;
    }

    const std::string project = arguments.Option("--project").value_or(std::string{});
    Steps steps{ project };
    const std::string scene = sceneFile;
    // Always removed again, whatever happens below.
    struct Cleanup {
        fs::path scene;
        ~Cleanup() {
            std::error_code ignored;
            fs::remove(scene, ignored);
            fs::path meta = scene;
            meta.replace_extension(".meta");
            fs::remove(meta, ignored);
        }
    } cleanup{ sceneDisk };

    bool built = steps.Run(&RunSceneNewCommand, { "--name", std::string{ kSceneName }, "--template", "Empty" });
    const double centerX = (static_cast<double>(columns) - 1.0) * kSpacing / 2.0;
    const double centerY = (static_cast<double>(rows) - 1.0) * kSpacing / 2.0;
    built = built && steps.Run(&RunEntityCommand, { "add", "--scene", scene, "--create", "Directional Light", "--name", "Sun", "--rotation", "40,-35,0" });
    // A second light from the other side, so the side the sun misses is still readable.
    built = built && steps.Run(&RunEntityCommand, { "add", "--scene", scene, "--create", "Directional Light", "--name", "Fill", "--rotation", "-25,145,0" });
    built = built && steps.Run(&RunComponentCommand, { "set", "--scene", scene, "--entity", "Fill", "--type", "Light", "--set", "intensity=0.5" });
    built = built && steps.Run(&RunEntityCommand, { "add", "--scene", scene, "--create", "Camera", "--name", "Eye",
        "--position", Number(centerX) + ",0," + Number(-12.0) });
    built = built && steps.Run(&RunComponentCommand, { "set", "--scene", scene, "--entity", "Eye", "--type", "Camera", "--set", "projection=Orthographic",
        "--set", "orthographicHeight=" + Number(static_cast<double>(rows) * kSpacing), "--set", "clearColor.x=0.16", "--set", "clearColor.y=0.17", "--set", "clearColor.z=0.2" });
    if (post || ui) {
        for (std::size_t index = 0U; built && index < 3U; ++index) {
            built = steps.Run(&RunEntityCommand, { "add", "--scene", scene, "--create", "Sphere", "--name", "Ball" + std::to_string(index),
                "--position", Number(static_cast<double>(index) * kSpacing) + ",0,0", "--color", index == 0U ? "#D94F4F" : index == 1U ? "#E8E8E8" : "#4F7FD9" });
        }
        if (post) {
            built = built && steps.Run(&RunEntityCommand, { "add", "--scene", scene, "--name", "Filter" });
            built = built && steps.Run(&RunComponentCommand, { "add", "--scene", scene, "--entity", "Filter", "--type", "MeshRenderer", "--set", "material=" + shown.front().path });
        } else {
            built = built && steps.Run(&RunEntityCommand, { "add", "--scene", scene, "--name", "Hud" });
            built = built && steps.Run(&RunComponentCommand, { "add", "--scene", scene, "--entity", "Hud", "--type", "Rect Transform" });
            built = built && steps.Run(&RunComponentCommand, { "add", "--scene", scene, "--entity", "Hud", "--type", "Canvas" });
            built = built && steps.Run(&RunEntityCommand, { "add", "--scene", scene, "--name", "Panel", "--parent", "Hud" });
            // The panel covers the middle of the view: from 15% to 85% across and 20% to 80% up (a canvas pixel is a picture pixel).
            built = built && steps.Run(&RunComponentCommand, { "add", "--scene", scene, "--entity", "Hud/Panel", "--type", "Rect Transform",
                "--set", "offsetMin.x=" + Number(width * 0.15), "--set", "offsetMin.y=" + Number(height * 0.2),
                "--set", "offsetMax.x=" + Number(width * 0.85), "--set", "offsetMax.y=" + Number(height * 0.8) });
            built = built && steps.Run(&RunComponentCommand, { "add", "--scene", scene, "--entity", "Hud/Panel", "--type", "Image", "--set", "materialAssetId=" + shown.front().path });
        }
    } else {
        for (std::size_t index = 0U; built && index < shown.size(); ++index) {
            const double x = static_cast<double>(index % columns) * kSpacing;
            const double y = (static_cast<double>(rows) - 1.0 - static_cast<double>(index / columns)) * kSpacing - centerY;
            built = steps.Run(&RunEntityCommand, { "add", "--scene", scene, "--create", "Sphere", "--name", "Ball" + std::to_string(index),
                "--position", Number(x) + "," + Number(y) + ",0", "--material", shown[index].path });
        }
    }
    if (!built) {
        io.err << "error: the preview scene could not be built: " << steps.Error();
        return 1;
    }

    std::vector<std::string> capture{ "--project", project, "--scene", scenePath, "--camera", "Eye", "--out", material::Utf8Text(picture), "--frames", arguments.Option("--frames").value_or("40"),
        "--width", std::to_string(width), "--height", std::to_string(height) };
    if (const std::optional<std::string> device = arguments.Option("--device"); device.has_value()) capture.insert(capture.end(), { "--device", *device });
    std::ostringstream captured;
    if (RunCaptureCommand(ArgumentList{ capture }, CommandIo{ .out = captured, .err = io.err }) != 0) return 1;
    std::string shownPath = material::Utf8Text(picture);
    if (const std::string relative = picture.lexically_relative(projectRoot).generic_string(); !relative.empty() && !relative.starts_with("..")) shownPath = relative;
    io.out << "preview of " << shown.size() << " material" << (shown.size() == 1U ? "" : "s") << " in " << shownPath << " (" << width << 'x' << height << ")\n";
    if (shown.size() > 1U) io.out << "left to right, top to bottom: " << [&]() { std::string names; for (const Previewed& item : shown) names += (names.empty() ? "" : ", ") + item.path.substr(item.path.rfind('/') + 1U); return names; }() << '\n';
    if (const std::string summary = CaptureSummary(picture); !summary.empty()) io.out << summary << '\n';
    return 0;
}

} // namespace kb::cli
