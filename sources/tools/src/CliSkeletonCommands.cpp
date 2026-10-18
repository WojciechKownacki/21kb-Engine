#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "engine/scene/SkeletonNames.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// skeleton: reads a character's bones and edits its sockets - named points on a bone (a hand's
// grip, a back's sheath) that SocketAttachment holds objects at. Sockets are not part of the
// skeleton's compatibility signature, so adding one never unbinds a character or its clips.

namespace kb::cli {
namespace {

constexpr std::string_view kUsage =
    "usage: kb_cli skeleton <show|socket> --project <dir> --skeleton <skeleton or skeletal mesh path> ...\n"
    "  show                     the bone tree and the sockets\n"
    "  socket add               --name <Socket> --bone <Bone> [--position x,y,z] [--rotation x,y,z (degrees)]\n"
    "  socket remove            --name <Socket>\n"
    "  Objects hang on a socket (or directly on a bone) with: kb_cli entity attach --name <Object> --to <Character> --socket <Name>\n";

struct Session {
    kb::scene::Scene scene;
    std::filesystem::path file;
    std::string virtualPath;
    kb::scene::SkeletonAsset skeleton;
};

[[nodiscard]] std::optional<kb::math::Vec3> ParseVec3(std::string_view text) {
    std::array<float, 3> values{};
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const std::size_t comma = text.find(',');
        const std::string_view part = text.substr(0, comma);
        const auto [end, code] = std::from_chars(part.data(), part.data() + part.size(), values[index]);
        if (code != std::errc{} || end != part.data() + part.size() || !std::isfinite(values[index])) return std::nullopt;
        if ((comma == std::string_view::npos) != (index + 1U == values.size())) return std::nullopt;
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1U);
    }
    return kb::math::Vec3{ values[0], values[1], values[2] };
}

[[nodiscard]] bool OpenSkeleton(Session& session, const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    const std::optional<std::string> path = arguments.Option("--skeleton");
    if (!project.has_value() || !path.has_value()) {
        io.err << "error: skeleton requires --project <dir> and --skeleton <path>\n" << kUsage;
        return false;
    }
    std::string error;
    if (!MountProjectAssets(session.scene, kb::assets::Utf8Path(*project), error)) {
        io.err << "error: " << error << '\n';
        return false;
    }
    const kb::assets::AssetRegistry& registry = session.scene.Assets().Manager().Registry();
    const kb::assets::AssetMetadata* metadata = FindAssetByFlexiblePath(registry, kb::assets::Utf8Path(*path));
    // A skeletal mesh stands for its skeleton: that is the asset an import names after the character.
    if (metadata != nullptr && metadata->type == kb::scene::kSkeletalMeshAssetType) {
        const std::optional<kb::scene::SkeletalMeshAssetBinding> binding =
            kb::scene::SkeletalMeshAssetIO::LoadBinding(metadata->physicalPath, &error);
        metadata = binding.has_value() ? registry.Find(kb::assets::AssetId{ binding->skeletonAssetId }) : nullptr;
    }
    if (metadata == nullptr || metadata->type != kb::scene::kSkeletonAssetType) {
        io.err << "error: '" << *path << "' is not a Skeleton (.kbskeleton) or Skeletal Mesh of the project"
               << " (import a character with kb_cli import)\n";
        return false;
    }
    std::optional<kb::scene::SkeletonAsset> loaded = kb::scene::SkeletonAssetIO::Load(metadata->physicalPath, &error);
    if (!loaded.has_value()) {
        io.err << "error: " << metadata->virtualPath.generic_string() << " could not be read: " << error << '\n';
        return false;
    }
    session.file = metadata->physicalPath;
    session.virtualPath = metadata->virtualPath.generic_string();
    session.skeleton = std::move(*loaded);
    return true;
}

// The bone the author means: its exact name, else the one bone it names written another way
// ("hand_r" for "hand.R", NormalizeSkeletonName).
[[nodiscard]] const kb::scene::SkeletonBone* FindBone(const kb::scene::SkeletonAsset& skeleton, std::string_view name, CommandIo io) {
    for (const kb::scene::SkeletonBone& bone : skeleton.bones) {
        if (bone.name == name) return &bone;
    }
    const std::string wanted = kb::scene::NormalizeSkeletonName(name);
    std::vector<const kb::scene::SkeletonBone*> folded;
    std::vector<std::string_view> similar;
    for (const kb::scene::SkeletonBone& bone : skeleton.bones) {
        const std::string normalized = kb::scene::NormalizeSkeletonName(bone.name);
        if (normalized == wanted) folded.push_back(&bone);
        else if (!wanted.empty() && (normalized.find(wanted) != std::string::npos || wanted.find(normalized) != std::string::npos)) {
            similar.push_back(bone.name);
        }
    }
    if (folded.size() == 1U) return folded.front();
    for (const kb::scene::SkeletonBone* bone : folded) similar.push_back(bone->name);
    io.err << "error: the skeleton has no bone named '" << name << "'";
    if (!similar.empty()) {
        io.err << ". Did you mean:";
        for (std::size_t index = 0U; index < similar.size() && index < 8U; ++index) io.err << " '" << similar[index] << "'";
    }
    io.err << " (all bones: kb_cli skeleton show)\n";
    return nullptr;
}

[[nodiscard]] bool Save(const Session& session, CommandIo io) {
    const kb::scene::SkeletonAssetValidationResult valid = kb::scene::ValidateSkeletonAsset(session.skeleton);
    if (!valid.valid) {
        io.err << "error: " << valid.error << '\n';
        return false;
    }
    if (!kb::scene::SkeletonAssetIO::Save(session.file, session.skeleton)) {
        io.err << "error: " << session.virtualPath << " could not be written\n";
        return false;
    }
    return true;
}

void PrintBone(const kb::scene::SkeletonAsset& skeleton, std::size_t index, std::size_t depth, std::ostream& out) {
    const kb::scene::SkeletonBone& bone = skeleton.bones[index];
    out << std::string(depth * 2U + 2U, ' ') << bone.name;
    for (const kb::scene::SkeletonSocket& socket : skeleton.sockets) {
        if (socket.boneId == bone.id) out << "  [socket " << socket.name << ']';
    }
    out << '\n';
    for (std::size_t child = index + 1U; child < skeleton.bones.size(); ++child) {
        if (skeleton.bones[child].parentIndex == static_cast<std::int32_t>(index)) PrintBone(skeleton, child, depth + 1U, out);
    }
}

[[nodiscard]] int Show(const ArgumentList& arguments, CommandIo io) {
    Session session;
    if (!OpenSkeleton(session, arguments, io)) return 1;
    io.out << session.virtualPath << ": " << session.skeleton.bones.size() << " bones, " << session.skeleton.sockets.size()
           << " sockets (any bone name works as a socket too)\n";
    for (std::size_t index = 0U; index < session.skeleton.bones.size(); ++index) {
        if (session.skeleton.bones[index].parentIndex < 0) PrintBone(session.skeleton, index, 0U, io.out);
    }
    return 0;
}

[[nodiscard]] int SocketAdd(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    const std::optional<std::string> boneName = arguments.Option("--bone");
    if (!name.has_value() || name->empty() || !boneName.has_value()) {
        io.err << "error: skeleton socket add requires --name <Socket> --bone <Bone>\n" << kUsage;
        return 1;
    }
    Session session;
    if (!OpenSkeleton(session, arguments, io)) return 1;
    const kb::scene::SkeletonBone* bone = FindBone(session.skeleton, *boneName, io);
    if (bone == nullptr) return 1;
    kb::scene::SkeletonSocket socket{ .name = *name, .boneId = bone->id };
    if (const std::optional<std::string> position = arguments.Option("--position"); position.has_value()) {
        const std::optional<kb::math::Vec3> value = ParseVec3(*position);
        if (!value.has_value()) {
            io.err << "error: --position expects x,y,z in meters\n";
            return 1;
        }
        socket.localTransform.position = *value;
    }
    if (const std::optional<std::string> rotation = arguments.Option("--rotation"); rotation.has_value()) {
        const std::optional<kb::math::Vec3> value = ParseVec3(*rotation);
        if (!value.has_value()) {
            io.err << "error: --rotation expects three angles in degrees, x,y,z\n";
            return 1;
        }
        socket.localTransform.rotation = kb::math::FromEulerDegrees(*value);
    }
    const auto existing = std::ranges::find_if(session.skeleton.sockets,
        [&](const kb::scene::SkeletonSocket& value) { return value.name == *name; });
    const bool replaced = existing != session.skeleton.sockets.end();
    if (replaced) *existing = socket;
    else session.skeleton.sockets.push_back(socket);
    if (!Save(session, io)) return 1;
    io.out << (replaced ? "updated socket '" : "added socket '") << *name << "' on bone '" << bone->name << "' ("
           << session.virtualPath << ")\n";
    return 0;
}

[[nodiscard]] int SocketRemove(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    if (!name.has_value()) {
        io.err << "error: skeleton socket remove requires --name <Socket>\n";
        return 1;
    }
    Session session;
    if (!OpenSkeleton(session, arguments, io)) return 1;
    const std::size_t removed = std::erase_if(session.skeleton.sockets,
        [&](const kb::scene::SkeletonSocket& value) { return value.name == *name; });
    if (removed == 0U) {
        io.err << "error: " << session.virtualPath << " has no socket '" << *name << "'\n";
        return 1;
    }
    if (!Save(session, io)) return 1;
    io.out << "removed socket '" << *name << "' (" << session.virtualPath << ")\n";
    return 0;
}

} // namespace

int RunSkeletonCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.empty() ? std::string{} : positionals[0];
    const std::string sub = positionals.size() > 1U ? positionals[1] : std::string{};
    if (action == "show") return Show(arguments, io);
    if (action == "socket" && sub == "add") return SocketAdd(arguments, io);
    if (action == "socket" && sub == "remove") return SocketRemove(arguments, io);
    io.err << kUsage;
    return 1;
}

} // namespace kb::cli
