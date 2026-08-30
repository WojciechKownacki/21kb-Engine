#include "engine/scene/SkeletonAssetIO.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include "scene/asset/io/VersionedTextAssetHeader.hpp"

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace kb::scene {
namespace {

[[nodiscard]] bool EndOfRecord(std::istringstream& input) {
    input >> std::ws;
    return input.eof() || input.peek() == '#';
}

[[nodiscard]] std::optional<std::string> ReadText(const std::filesystem::path& path) {
    const std::vector<std::uint8_t> bytes = SceneAssetBinaryIO::ReadAllBytes(path);
    if (bytes.empty()) return std::nullopt;
    return std::string{ reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

[[nodiscard]] bool WriteText(const std::filesystem::path& path, const std::string& text) {
    return SceneAssetBinaryIO::WriteBytesAtomically(
        path, std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
}

} // namespace

std::optional<SkeletonAsset> SkeletonAssetIO::Load(
    const std::filesystem::path& path,
    std::string* error) {
    const auto fail = [error](std::string message) -> std::optional<SkeletonAsset> {
        if (error != nullptr) *error = std::move(message);
        return std::nullopt;
    };
    if (error != nullptr) error->clear();
    if (path.extension() != kSkeletonAssetExtension) {
        return fail("Skeleton asset has an unexpected file extension.");
    }
    const std::optional<std::string> text = ReadText(path);
    if (!text) return fail("Skeleton asset could not be read.");
    return Load(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(text->data()), text->size() }, error);
}

std::optional<SkeletonAsset> SkeletonAssetIO::Load(
    std::span<const std::uint8_t> bytes,
    std::string* error) {
    const auto fail = [error](std::string message) -> std::optional<SkeletonAsset> {
        if (error != nullptr) *error = std::move(message);
        return std::nullopt;
    };
    if (bytes.empty()) {
        return fail("Skeleton asset could not be read.");
    }
    const std::string text{ reinterpret_cast<const char*>(bytes.data()), bytes.size() };

    SkeletonAsset asset{};
    std::istringstream file{ text };
    file.imbue(std::locale::classic());
    std::string line;
    bool schemaRead = false;
    std::size_t lineNumber = 0U;
    while (std::getline(file, line)) {
        ++lineNumber;
        std::istringstream input{ line };
        input.imbue(std::locale::classic());
        std::string command;
        if (!(input >> command) || command.starts_with('#')) continue;
        if (error != nullptr) {
            *error = "Skeleton asset has an invalid record at line " +
                std::to_string(lineNumber) + ".";
        }
        if (!schemaRead) {
            const asset_io::TextAssetHeaderStatus header =
                asset_io::ParseTextAssetHeader(
                    line, kSkeletonAssetType, kSkeletonAssetSchemaVersion);
            if (header == asset_io::TextAssetHeaderStatus::Invalid) return std::nullopt;
            schemaRead = true;
            if (header == asset_io::TextAssetHeaderStatus::Current) continue;
        }
        if (command == "socket") {
            SkeletonSocket socket{};
            if (!(input >> std::quoted(socket.name) >> socket.boneId >>
                    socket.localTransform.position.x >> socket.localTransform.position.y >> socket.localTransform.position.z >>
                    socket.localTransform.rotation.x >> socket.localTransform.rotation.y >> socket.localTransform.rotation.z >> socket.localTransform.rotation.w >>
                    socket.localTransform.scale.x >> socket.localTransform.scale.y >> socket.localTransform.scale.z) ||
                !EndOfRecord(input)) {
                return std::nullopt;
            }
            asset.sockets.push_back(std::move(socket));
            continue;
        }
        if (command != "bone") return std::nullopt;

        SkeletonBone bone{};
        if (!(input >> bone.id >> bone.parentIndex >> std::quoted(bone.name) >>
                bone.referencePose.position.x >> bone.referencePose.position.y >> bone.referencePose.position.z >>
                bone.referencePose.rotation.x >> bone.referencePose.rotation.y >> bone.referencePose.rotation.z >> bone.referencePose.rotation.w >>
                bone.referencePose.scale.x >> bone.referencePose.scale.y >> bone.referencePose.scale.z) ||
            !EndOfRecord(input)) {
            return std::nullopt;
        }

        if (!std::getline(file, line)) return std::nullopt;
        ++lineNumber;
        std::istringstream matrixInput{ line };
        matrixInput.imbue(std::locale::classic());
        if (!(matrixInput >> command) || command != "inverseBind") return std::nullopt;
        for (kb::math::Vec4& column : bone.inverseBind.columns) {
            if (!(matrixInput >> column.x >> column.y >> column.z >> column.w)) return std::nullopt;
        }
        if (!EndOfRecord(matrixInput)) return std::nullopt;
        asset.bones.push_back(std::move(bone));
    }

    const SkeletonAssetValidationResult validation = ValidateSkeletonAsset(asset);
    if (!validation.valid) return fail(validation.error);
    if (error != nullptr) error->clear();
    return asset;
}

bool SkeletonAssetIO::Save(const std::filesystem::path& path, const SkeletonAsset& asset) {
    if (path.extension() != kSkeletonAssetExtension || !ValidateSkeletonAsset(asset).valid) return false;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    output << asset_io::TextAssetHeader(
        kSkeletonAssetType, kSkeletonAssetSchemaVersion);
    for (const SkeletonBone& bone : asset.bones) {
        const LocalTransform& pose = bone.referencePose;
        output << "bone " << bone.id << ' ' << bone.parentIndex << ' ' << std::quoted(bone.name) << ' '
               << pose.position.x << ' ' << pose.position.y << ' ' << pose.position.z << ' '
               << pose.rotation.x << ' ' << pose.rotation.y << ' ' << pose.rotation.z << ' ' << pose.rotation.w << ' '
               << pose.scale.x << ' ' << pose.scale.y << ' ' << pose.scale.z << '\n';
        output << "inverseBind";
        for (const kb::math::Vec4& column : bone.inverseBind.columns) {
            output << ' ' << column.x << ' ' << column.y << ' ' << column.z << ' ' << column.w;
        }
        output << '\n';
    }
    for (const SkeletonSocket& socket : asset.sockets) {
        const LocalTransform& transform = socket.localTransform;
        output << "socket " << std::quoted(socket.name) << ' ' << socket.boneId << ' '
               << transform.position.x << ' ' << transform.position.y << ' ' << transform.position.z << ' '
               << transform.rotation.x << ' ' << transform.rotation.y << ' ' << transform.rotation.z << ' ' << transform.rotation.w << ' '
               << transform.scale.x << ' ' << transform.scale.y << ' ' << transform.scale.z << '\n';
    }
    return WriteText(path, output.str());
}

} // namespace kb::scene
