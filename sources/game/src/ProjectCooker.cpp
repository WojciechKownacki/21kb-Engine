#include "ProjectCooker.hpp"
#include "PackagedRuntimeModuleContract.hpp"

#include "engine/assets/AssetCompatibility.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMemoryInputStream.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/ImportedAsset.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "engine/assets/bake/AssetPackWriter.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/assets/bake/RuntimeAssetManifest.hpp"
#include "engine/project/ParticleProjectPolicy.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssetMeta.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/ShaderManifest.hpp"
#include "kb/render/bake/MeshBaker.hpp"
#include "kb/render/bake/RuntimeAssetPackValidation.hpp"
#include "kb/render/bake/TextureBaker.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialCookPayload.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeRenderAssetDiscovery.hpp"

#include <bimg/encode.h>
#include <bx/error.h>
#include <bx/readerwriter.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#else
    #include <fcntl.h>
    #include <spawn.h>
    #include <sys/file.h>
    #include <sys/wait.h>
    #include <unistd.h>
    extern char** environ;
#endif

namespace kb::game {
namespace {

#ifndef KB_COOKER_DEFAULT_SHADERC
    #define KB_COOKER_DEFAULT_SHADERC ""
#endif
#ifndef KB_COOKER_DEFAULT_ENGINE_ROOT
    #define KB_COOKER_DEFAULT_ENGINE_ROOT ""
#endif

namespace asset_bake = kb::assets::bake;
namespace render_bake = kb::render::bake;

constexpr std::string_view kSourceFileBakerId = "SourceFile";
constexpr std::string_view kShaderBinaryBakerId = "ShaderBinary";
constexpr std::string_view kRuntimeManifestBakerId = "RuntimeManifest";
constexpr std::string_view kStoredBytesBakerVersion = "1";
constexpr std::array<std::string_view, 4U> kMaterialPasses{
    "BaseOpaque", "GBuffer", "ShadowDepth", "BaseTransparent"
};

class VectorWriter final : public bx::WriterI {
public:
    explicit VectorWriter(std::vector<std::uint8_t>& target) noexcept
        : target_{ &target } {}

    std::int32_t write(const void* data, std::int32_t size, bx::Error* error) override {
        BX_UNUSED(error);
        if (data == nullptr || size <= 0) {
            return 0;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        target_->insert(target_->end(), bytes, bytes + size);
        return size;
    }

private:
    std::vector<std::uint8_t>* target_ = nullptr;
};

[[nodiscard]] ProjectCookResult Failure(std::string error) {
    return ProjectCookResult{ .error = std::move(error) };
}

[[nodiscard]] bool ReadFileBytes(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& bytes,
    std::string& error) {
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
    constexpr std::uint64_t kEnvelopeBytes = 16U;
    if (sizeError || size > asset_bake::kMaxAssetPackBlockBytes - kEnvelopeBytes ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        error = "file is unreadable or exceeds the package block budget: " + path.generic_string();
        return false;
    }
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        error = "file could not be opened: " + path.generic_string();
        return false;
    }
    bytes.assign(static_cast<std::size_t>(size), 0U);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if ((!bytes.empty() && input.gcount() != static_cast<std::streamsize>(bytes.size())) || input.bad()) {
        bytes.clear();
        error = "file could not be read completely: " + path.generic_string();
        return false;
    }
    return true;
}

struct SourceSnapshot {
    std::filesystem::path path;
    struct TrackedFile {
        std::filesystem::path sourcePath;
        std::filesystem::path snapshotPath;
        std::uint64_t discoveryHash = 0U;
    };
    std::vector<TrackedFile> trackedFiles;
};

using SourceSnapshotMap = std::unordered_map<std::uint64_t, SourceSnapshot>;

class ScopedCookDirectory final {
public:
    explicit ScopedCookDirectory(std::filesystem::path path) noexcept
        : path_{ std::move(path) } {}
    ScopedCookDirectory(const ScopedCookDirectory&) = delete;
    ScopedCookDirectory& operator=(const ScopedCookDirectory&) = delete;
    ~ScopedCookDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::filesystem::path WithCookFileSuffix(
    const std::filesystem::path& path,
    std::string_view suffix) {
#if defined(_WIN32)
    std::wstring wideSuffix;
    wideSuffix.reserve(suffix.size());
    for (const char character : suffix) {
        wideSuffix.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
    }
    return std::filesystem::path{ path.native() + wideSuffix };
#else
    return std::filesystem::path{ path.native() + std::string{ suffix } };
#endif
}

class ScopedCookOutputLock final {
public:
    ScopedCookOutputLock() = default;
    ScopedCookOutputLock(const ScopedCookOutputLock&) = delete;
    ScopedCookOutputLock& operator=(const ScopedCookOutputLock&) = delete;
    ~ScopedCookOutputLock() {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(CloseHandle(handle_));
            handle_ = INVALID_HANDLE_VALUE;
            std::error_code removeError;
            std::filesystem::remove(lockPath_, removeError);
        }
#else
        if (descriptor_ >= 0) {
            static_cast<void>(flock(descriptor_, LOCK_UN));
            static_cast<void>(close(descriptor_));
            descriptor_ = -1;
            // Do not unlink POSIX advisory lock files. Removing a locked inode lets
            // another process create and lock a different inode for the same output.
        }
#endif
    }

    [[nodiscard]] bool Acquire(const std::filesystem::path& outputPath, std::string& error) {
        if (outputPath.empty() || outputPath.filename().empty()) {
            error = "output package path is empty or has no filename";
            return false;
        }
        std::filesystem::path parent = outputPath.parent_path();
        if (parent.empty()) {
            parent = ".";
        }
        std::error_code directoryError;
        std::filesystem::create_directories(parent, directoryError);
        if (directoryError) {
            error = "output package directory could not be created: " + parent.generic_string();
            return false;
        }
        lockPath_ = WithCookFileSuffix(outputPath, ".kbpacklock");
#if defined(_WIN32)
        handle_ = CreateFileW(
            lockPath_.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            error = "output package is already being cooked or its publication lock is unavailable: " +
                outputPath.generic_string();
            return false;
        }
#else
        descriptor_ = open(lockPath_.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
        if (descriptor_ < 0 || flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            if (descriptor_ >= 0) {
                static_cast<void>(close(descriptor_));
                descriptor_ = -1;
            }
            error = "output package is already being cooked or its publication lock is unavailable: " +
                outputPath.generic_string();
            return false;
        }
#endif
        return true;
    }

private:
    std::filesystem::path lockPath_;
#if defined(_WIN32)
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

class ScopedCookPackCandidate final {
public:
    explicit ScopedCookPackCandidate(std::filesystem::path path) noexcept
        : path_{ std::move(path) } {}
    ScopedCookPackCandidate(const ScopedCookPackCandidate&) = delete;
    ScopedCookPackCandidate& operator=(const ScopedCookPackCandidate&) = delete;
    ~ScopedCookPackCandidate() {
        std::error_code error;
        std::filesystem::remove(path_, error);
        for (const std::string_view suffix : {
                 std::string_view{ ".kbpackstaging" },
                 std::string_view{ ".kbpackpayload" },
                 std::string_view{ ".kbpacklock" } }) {
            error.clear();
            std::filesystem::remove(WithCookFileSuffix(path_, suffix), error);
        }
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::filesystem::path CreateCookPackCandidatePath(
    const std::filesystem::path& outputPath,
    std::string& error) {
    std::filesystem::path parent = outputPath.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
#if defined(_WIN32)
    const std::uint64_t processId = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const std::uint64_t processId = static_cast<std::uint64_t>(getpid());
#endif
    static std::atomic<std::uint64_t> nextCandidate{ 0U };
    for (std::uint32_t attempt = 0U; attempt < 256U; ++attempt) {
        const std::uint64_t sequence = nextCandidate.fetch_add(1U, std::memory_order_relaxed);
        const std::filesystem::path candidate = parent /
            (".kb-cook-" + std::to_string(processId) + "-" + std::to_string(sequence) + ".kbpack");
        if (candidate.lexically_normal() == outputPath.lexically_normal()) {
            continue;
        }
        bool available = true;
        std::error_code statusError;
        for (const std::filesystem::path& path : {
                 candidate,
                 WithCookFileSuffix(candidate, ".kbpackstaging"),
                 WithCookFileSuffix(candidate, ".kbpackpayload"),
                 WithCookFileSuffix(candidate, ".kbpacklock") }) {
            const std::filesystem::file_status status = std::filesystem::symlink_status(path, statusError);
            if (statusError && statusError != std::errc::no_such_file_or_directory) {
                error = "cook package candidate path could not be inspected: " + path.generic_string();
                return {};
            }
            statusError.clear();
            if (status.type() != std::filesystem::file_type::not_found) {
                available = false;
                break;
            }
        }
        if (available) {
            return candidate;
        }
    }
    error = "a unique cook package candidate path could not be allocated";
    return {};
}

[[nodiscard]] bool PublishValidatedCookPack(
    const std::filesystem::path& candidate,
    const std::filesystem::path& destination) noexcept {
#if defined(_WIN32)
    return MoveFileExW(
        candidate.c_str(), destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    return ::rename(candidate.c_str(), destination.c_str()) == 0;
#endif
}

[[nodiscard]] std::filesystem::path CreateCookSnapshotDirectory(
    const std::filesystem::path& cacheRoot,
    std::string_view category,
    std::string& error) {
    const std::filesystem::path parent = cacheRoot / category;
    std::error_code directoryError;
    std::filesystem::create_directories(parent, directoryError);
    if (directoryError) {
        error = "cook snapshot directory could not be created: " + parent.generic_string();
        return {};
    }
#if defined(_WIN32)
    const std::uint64_t processId = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const std::uint64_t processId = static_cast<std::uint64_t>(getpid());
#endif
    static std::atomic<std::uint64_t> nextDirectory{ 0U };
    for (std::uint32_t attempt = 0U; attempt < 256U; ++attempt) {
        const std::uint64_t sequence = nextDirectory.fetch_add(1U, std::memory_order_relaxed);
        const std::filesystem::path candidate =
            parent / (std::to_string(processId) + "-" + std::to_string(sequence));
        directoryError.clear();
        if (std::filesystem::create_directory(candidate, directoryError)) {
            return candidate;
        }
        if (directoryError) {
            error = "cook snapshot directory could not be created: " + candidate.generic_string();
            return {};
        }
    }
    error = "a unique cook snapshot directory could not be allocated";
    return {};
}

[[nodiscard]] bool CopySourceSnapshot(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::uint64_t& discoveryHash,
    std::string& error) {
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(source, sizeError);
    constexpr std::uint64_t kEnvelopeBytes = 16U;
    if (sizeError || size > asset_bake::kMaxAssetPackBlockBytes - kEnvelopeBytes) {
        error = "file is unreadable or exceeds the package block budget: " + source.generic_string();
        return false;
    }
    std::error_code directoryError;
    std::filesystem::create_directories(destination.parent_path(), directoryError);
    if (directoryError) {
        error = "source snapshot parent directory could not be created: " +
            destination.parent_path().generic_string();
        return false;
    }
    std::ifstream input{ source, std::ios::binary };
    std::ofstream output{ destination, std::ios::binary | std::ios::trunc };
    if (!input.is_open() || !output.is_open()) {
        error = "source snapshot file could not be opened: " + source.generic_string();
        return false;
    }

    std::array<std::uint8_t, 64U * 1024U> buffer{};
    std::uintmax_t copied = 0U;
    std::uint64_t hash = 14695981039346656037ULL;
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = input.gcount();
        if (read <= 0) break;
        output.write(reinterpret_cast<const char*>(buffer.data()), read);
        if (!output) {
            error = "source snapshot file could not be written: " + destination.generic_string();
            return false;
        }
        copied += static_cast<std::uintmax_t>(read);
        for (std::streamsize index = 0; index < read; ++index) {
            hash ^= buffer[static_cast<std::size_t>(index)];
            hash *= 1099511628211ULL;
        }
    }
    output.flush();
    if (input.bad() || !input.eof() || !output || copied != size) {
        error = "source changed or could not be copied completely: " + source.generic_string();
        return false;
    }
    discoveryHash = hash == 0U ? 1099511628211ULL : hash;
    return true;
}

[[nodiscard]] bool CanonicalRelativePathWithin(
    const std::filesystem::path& child,
    const std::filesystem::path& parent,
    std::filesystem::path& relative) {
    std::error_code childError;
    std::error_code parentError;
    const std::filesystem::path canonicalChild = std::filesystem::weakly_canonical(child, childError);
    const std::filesystem::path canonicalParent = std::filesystem::weakly_canonical(parent, parentError);
    if (childError || parentError) return false;
    relative = canonicalChild.lexically_relative(canonicalParent);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

[[nodiscard]] bool HashSourceFile(
    const std::filesystem::path& source,
    std::uint64_t& discoveryHash) {
    std::ifstream input{ source, std::ios::binary };
    if (!input.is_open()) return false;
    std::array<std::uint8_t, 64U * 1024U> buffer{};
    std::uint64_t hash = 14695981039346656037ULL;
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = input.gcount();
        if (read <= 0) break;
        for (std::streamsize index = 0; index < read; ++index) {
            hash ^= buffer[static_cast<std::size_t>(index)];
            hash *= 1099511628211ULL;
        }
    }
    if (input.bad() || !input.eof()) return false;
    discoveryHash = hash == 0U ? 1099511628211ULL : hash;
    return true;
}

struct CookConfigurationInputFingerprint {
    std::filesystem::path path;
    bool present = false;
    std::uint64_t contentHash = 0U;
};

using CookConfigurationSnapshot = std::array<CookConfigurationInputFingerprint, 3U>;

[[nodiscard]] bool CaptureCookConfigurationInput(
    const std::filesystem::path& path,
    bool required,
    CookConfigurationInputFingerprint& fingerprint,
    std::string& error) {
    fingerprint = CookConfigurationInputFingerprint{ .path = path };
    std::error_code statusError;
    const std::filesystem::file_status status = std::filesystem::status(path, statusError);
    if (statusError && statusError != std::errc::no_such_file_or_directory) {
        error = "cook configuration input could not be inspected: " + path.generic_string();
        return false;
    }
    if (statusError == std::errc::no_such_file_or_directory ||
        status.type() == std::filesystem::file_type::not_found) {
        if (required) {
            error = "required cook configuration input is missing: " + path.generic_string();
            return false;
        }
        return true;
    }
    if (!std::filesystem::is_regular_file(status)) {
        error = "cook configuration input is not a regular file: " + path.generic_string();
        return false;
    }
    fingerprint.present = true;
    if (!HashSourceFile(path, fingerprint.contentHash)) {
        error = "cook configuration input could not be fingerprinted: " + path.generic_string();
        return false;
    }
    return true;
}

[[nodiscard]] bool VerifyCookConfigurationInputsUnchanged(
    const CookConfigurationSnapshot& snapshot,
    std::string& error) {
    for (const CookConfigurationInputFingerprint& expected : snapshot) {
        CookConfigurationInputFingerprint current;
        if (!CaptureCookConfigurationInput(expected.path, false, current, error)) {
            return false;
        }
        if (current.present != expected.present ||
            (current.present && current.contentHash != expected.contentHash)) {
            error = "cook configuration input changed while cooking: " +
                expected.path.generic_string();
            return false;
        }
    }
    return true;
}

struct CookerToolInputFingerprint {
    std::filesystem::path path;
    std::filesystem::path snapshotPath;
    std::uint64_t contentHash = 0U;
    std::filesystem::file_time_type lastWriteTime{};
};

struct CookerToolInputSnapshot {
    std::vector<CookerToolInputFingerprint> inputs;
    std::filesystem::path shaderSourceRoot;
    std::filesystem::path bgfxIncludeRoot;
};

[[nodiscard]] bool CollectCookerToolInputPaths(
    const ProjectCookRequest& request,
    std::vector<std::filesystem::path>& paths,
    std::string& error) {
    paths.clear();
    if (request.shadercPath.empty() || !std::filesystem::is_regular_file(request.shadercPath)) {
        error = "the pinned renderer shader compiler was not found";
        return false;
    }
    paths.push_back(request.shadercPath);
    for (const std::string_view siblingName : { std::string_view{ "dxcompiler.dll" },
                                                std::string_view{ "libdxcompiler.so" } }) {
        const std::filesystem::path sibling = request.shadercPath.parent_path() / siblingName;
        std::error_code typeError;
        if (std::filesystem::is_regular_file(sibling, typeError) && !typeError) {
            paths.push_back(sibling);
        }
    }
    const std::array<std::filesystem::path, 2U> roots{
        request.engineRoot / "sources" / "renderer" / "shaders",
        request.engineRoot / "third_party" / "bgfx.cmake" / "bgfx" / "src",
    };
    for (const std::filesystem::path& root : roots) {
        if (!std::filesystem::is_directory(root)) {
            error = "renderer shader input directory is unavailable: " + root.generic_string();
            return false;
        }
        std::error_code iteratorError;
        std::filesystem::recursive_directory_iterator iterator{ root, iteratorError };
        const std::filesystem::recursive_directory_iterator end;
        if (iteratorError) {
            error = "renderer shader input directory could not be enumerated: " + root.generic_string();
            return false;
        }
        for (; iterator != end; iterator.increment(iteratorError)) {
            if (iteratorError) {
                error = "renderer shader input directory changed while it was enumerated: " +
                    root.generic_string();
                return false;
            }
            std::error_code typeError;
            if (iterator->is_regular_file(typeError) && !typeError) {
                std::filesystem::path ignored;
                if (!CanonicalRelativePathWithin(iterator->path(), root, ignored)) {
                    error = "renderer shader input resolves outside its declared root: " +
                        iterator->path().generic_string();
                    return false;
                }
                paths.push_back(iterator->path());
            }
        }
        if (iteratorError) {
            error = "renderer shader input directory changed while it was enumerated: " +
                root.generic_string();
            return false;
        }
    }
    std::ranges::sort(paths, [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
        return lhs.generic_string() < rhs.generic_string();
    });
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return true;
}

[[nodiscard]] bool CaptureCookerToolInputs(
    const ProjectCookRequest& request,
    const std::filesystem::path& snapshotRoot,
    CookerToolInputSnapshot& snapshot,
    std::string& error) {
    snapshot = {};
    snapshot.shaderSourceRoot = snapshotRoot / "renderer-shaders";
    snapshot.bgfxIncludeRoot = snapshotRoot / "bgfx-src";
    const std::array<std::filesystem::path, 2U> roots{
        request.engineRoot / "sources" / "renderer" / "shaders",
        request.engineRoot / "third_party" / "bgfx.cmake" / "bgfx" / "src",
    };
    const std::array<std::filesystem::path, 2U> capturedRoots{
        snapshot.shaderSourceRoot,
        snapshot.bgfxIncludeRoot,
    };
    std::error_code directoryError;
    for (const std::filesystem::path& root : capturedRoots) {
        std::filesystem::create_directories(root, directoryError);
        if (directoryError) {
            error = "renderer shader snapshot directory could not be created: " + root.generic_string();
            return false;
        }
    }

    std::vector<std::filesystem::path> paths;
    if (!CollectCookerToolInputPaths(request, paths, error)) {
        return false;
    }
    snapshot.inputs.reserve(paths.size());
    for (const std::filesystem::path& path : paths) {
        std::filesystem::path capturedPath;
        for (std::size_t rootIndex = 0U; rootIndex < roots.size(); ++rootIndex) {
            std::filesystem::path relative;
            if (CanonicalRelativePathWithin(path, roots[rootIndex], relative)) {
                const std::filesystem::path lexicalRelative = path.lexically_relative(roots[rootIndex]);
                if (lexicalRelative.empty() || lexicalRelative.is_absolute() ||
                    *lexicalRelative.begin() == "..") {
                    error = "renderer shader input has no safe snapshot path: " + path.generic_string();
                    return false;
                }
                capturedPath = capturedRoots[rootIndex] / lexicalRelative;
                break;
            }
        }

        std::uint64_t snapshotHash = 0U;
        if (!capturedPath.empty() &&
            !CopySourceSnapshot(path, capturedPath, snapshotHash, error)) {
            return false;
        }
        std::uint64_t currentHash = 0U;
        std::error_code timeError;
        const std::filesystem::file_time_type lastWriteTime =
            std::filesystem::last_write_time(path, timeError);
        if (timeError || !HashSourceFile(path, currentHash) ||
            (!capturedPath.empty() && currentHash != snapshotHash)) {
            error = "renderer shader compiler input could not be fingerprinted: " + path.generic_string();
            return false;
        }
        snapshot.inputs.push_back(CookerToolInputFingerprint{
            .path = path,
            .snapshotPath = capturedPath,
            .contentHash = currentHash,
            .lastWriteTime = lastWriteTime,
        });
    }
    return true;
}

[[nodiscard]] bool VerifyCookerToolInputsUnchanged(
    const ProjectCookRequest& request,
    const CookerToolInputSnapshot& snapshot,
    std::string& error) {
    std::vector<std::filesystem::path> currentPaths;
    if (!CollectCookerToolInputPaths(request, currentPaths, error)) {
        return false;
    }
    if (currentPaths.size() != snapshot.inputs.size() ||
        !std::ranges::equal(currentPaths, snapshot.inputs, {},
            [](const std::filesystem::path& path) { return path.generic_string(); },
            [](const CookerToolInputFingerprint& input) { return input.path.generic_string(); })) {
        error = "renderer shader compiler input set changed while cooking";
        return false;
    }
    for (const CookerToolInputFingerprint& input : snapshot.inputs) {
        std::uint64_t currentHash = 0U;
        std::error_code timeError;
        const std::filesystem::file_time_type currentWriteTime =
            std::filesystem::last_write_time(input.path, timeError);
        if (timeError || currentWriteTime != input.lastWriteTime ||
            !HashSourceFile(input.path, currentHash) || currentHash != input.contentHash) {
            error = "renderer shader compiler input changed while cooking: " +
                input.path.generic_string();
            return false;
        }
        if (!input.snapshotPath.empty()) {
            std::uint64_t snapshotHash = 0U;
            if (!HashSourceFile(input.snapshotPath, snapshotHash) || snapshotHash != input.contentHash) {
                error = "renderer shader input snapshot changed while cooking: " +
                    input.snapshotPath.generic_string();
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool VerifyCookerExecutablesUnchanged(
    const CookerToolInputSnapshot& snapshot,
    std::string& error) {
    for (const CookerToolInputFingerprint& input : snapshot.inputs) {
        if (!input.snapshotPath.empty()) {
            continue;
        }
        std::uint64_t currentHash = 0U;
        std::error_code timeError;
        const std::filesystem::file_time_type currentWriteTime =
            std::filesystem::last_write_time(input.path, timeError);
        if (timeError || currentWriteTime != input.lastWriteTime ||
            !HashSourceFile(input.path, currentHash) || currentHash != input.contentHash) {
            error = "renderer shader compiler executable changed while cooking: " +
                input.path.generic_string();
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CaptureSourceSnapshots(
    std::span<const kb::assets::AssetMetadata> assets,
    const std::filesystem::path& contentRoot,
    const std::filesystem::path& snapshotRoot,
    SourceSnapshotMap& snapshots,
    std::string& error) {
    snapshots.clear();
    snapshots.reserve(assets.size());
    for (const kb::assets::AssetMetadata& metadata : assets) {
        std::filesystem::path relativeSource;
        if (!CanonicalRelativePathWithin(metadata.physicalPath, contentRoot, relativeSource)) {
            error = "asset source resolves outside the project content root: " +
                kb::assets::NormalizeAssetPath(metadata.virtualPath);
            return false;
        }
        const std::filesystem::path assetSnapshotRoot =
            snapshotRoot / std::to_string(metadata.id.value);
        const std::filesystem::path snapshotPath = assetSnapshotRoot / relativeSource;
        std::uint64_t snapshotHash = 0U;
        if (!CopySourceSnapshot(metadata.physicalPath, snapshotPath, snapshotHash, error)) {
            error = "asset source snapshot failed for " +
                kb::assets::NormalizeAssetPath(metadata.virtualPath) + ": " + error;
            return false;
        }
        if (metadata.contentHash == 0U || snapshotHash != metadata.contentHash) {
            error = "asset source changed after discovery: " +
                kb::assets::NormalizeAssetPath(metadata.virtualPath);
            return false;
        }
        SourceSnapshot sourceSnapshot{
            .path = snapshotPath,
            .trackedFiles = { SourceSnapshot::TrackedFile{
                .sourcePath = metadata.physicalPath,
                .snapshotPath = snapshotPath,
                .discoveryHash = snapshotHash,
            } },
        };

        // A scene's sidecar is an authored cooker input: it defines the dependency closure
        // and authenticates the scene bytes. It is intentionally not shipped, but it must be
        // captured and rechecked like the source so a concurrent save cannot publish a pack
        // whose manifest describes an older .meta state.
        if (metadata.type == "Scene") {
            const std::filesystem::path metaSource = kb::scene::SceneAssetMetaPath(metadata.physicalPath);
            std::filesystem::path relativeMeta;
            if (!CanonicalRelativePathWithin(metaSource, contentRoot, relativeMeta)) {
                error = "scene metadata sidecar resolves outside the project content root: " +
                    kb::assets::NormalizeAssetPath(metadata.virtualPath);
                return false;
            }
            const std::filesystem::path metaSnapshot = assetSnapshotRoot / relativeMeta;
            std::uint64_t metaHash = 0U;
            if (!CopySourceSnapshot(metaSource, metaSnapshot, metaHash, error)) {
                error = "scene metadata sidecar snapshot failed for " +
                    kb::assets::NormalizeAssetPath(metadata.virtualPath) + ": " + error;
                return false;
            }
            sourceSnapshot.trackedFiles.push_back(SourceSnapshot::TrackedFile{
                .sourcePath = metaSource,
                .snapshotPath = metaSnapshot,
                .discoveryHash = metaHash,
            });
        }

        std::string extension = metadata.physicalPath.extension().generic_string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        if (metadata.type == "RenderMesh" && extension == ".gltf") {
            std::vector<std::uint8_t> gltfBytes;
            if (!ReadFileBytes(snapshotPath, gltfBytes, error)) {
                error = "glTF source snapshot could not be read: " +
                    kb::assets::NormalizeAssetPath(metadata.virtualPath);
                return false;
            }
            const std::optional<std::vector<std::filesystem::path>> externalUris =
                kb::render::RenderMeshAssetBuilder::GltfExternalBufferUris(gltfBytes);
            if (!externalUris.has_value()) {
                error = "glTF contains an invalid or unsupported external buffer URI: " +
                    kb::assets::NormalizeAssetPath(metadata.virtualPath);
                return false;
            }
            for (const std::filesystem::path& uri : *externalUris) {
                const std::filesystem::path externalSource =
                    (metadata.physicalPath.parent_path() / uri).lexically_normal();
                std::filesystem::path relativeExternal;
                if (!CanonicalRelativePathWithin(externalSource, contentRoot, relativeExternal)) {
                    error = "glTF external buffer resolves outside the project content root: " +
                        externalSource.generic_string();
                    return false;
                }
                const std::filesystem::path externalSnapshot =
                    assetSnapshotRoot / relativeExternal;
                std::uint64_t externalHash = 0U;
                if (!CopySourceSnapshot(externalSource, externalSnapshot, externalHash, error)) {
                    error = "glTF external buffer snapshot failed: " + error;
                    return false;
                }
                sourceSnapshot.trackedFiles.push_back(SourceSnapshot::TrackedFile{
                    .sourcePath = externalSource,
                    .snapshotPath = externalSnapshot,
                    .discoveryHash = externalHash,
                });
            }
        }

        if (!snapshots.emplace(metadata.id.value, std::move(sourceSnapshot)).second) {
            error = "runtime closure contains a duplicate asset id: " + kb::assets::ToString(metadata.id);
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool VerifySourceSnapshotIntegrity(
    const SourceSnapshot& snapshot,
    std::string& error) {
    for (const SourceSnapshot::TrackedFile& tracked : snapshot.trackedFiles) {
        std::uint64_t snapshotHash = 0U;
        if (!HashSourceFile(tracked.snapshotPath, snapshotHash) ||
            snapshotHash != tracked.discoveryHash) {
            error = "captured asset source snapshot was modified: " +
                tracked.snapshotPath.generic_string();
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool VerifySnapshotSourcesUnchanged(
    std::span<const kb::assets::AssetMetadata> assets,
    const SourceSnapshotMap& snapshots,
    std::string& error) {
    for (const kb::assets::AssetMetadata& metadata : assets) {
        const auto snapshot = snapshots.find(metadata.id.value);
        if (snapshot == snapshots.end()) {
            error = "asset source snapshot disappeared: " + kb::assets::ToString(metadata.id);
            return false;
        }
        if (!VerifySourceSnapshotIntegrity(snapshot->second, error)) return false;
        for (const SourceSnapshot::TrackedFile& tracked : snapshot->second.trackedFiles) {
            std::uint64_t currentHash = 0U;
            if (!HashSourceFile(tracked.sourcePath, currentHash) ||
                currentHash != tracked.discoveryHash) {
                error = "asset source changed while cooking: " +
                    kb::assets::NormalizeAssetPath(metadata.virtualPath) +
                    " (" + tracked.sourcePath.generic_string() + ")";
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool ReadSourceSnapshot(
    const SourceSnapshotMap& snapshots,
    std::uint64_t assetId,
    std::vector<std::uint8_t>& bytes,
    std::string& error) {
    const auto snapshot = snapshots.find(assetId);
    if (snapshot == snapshots.end()) {
        error = "asset source snapshot is missing: " + std::to_string(assetId);
        return false;
    }
    if (!VerifySourceSnapshotIntegrity(snapshot->second, error)) return false;
    return ReadFileBytes(snapshot->second.path, bytes, error);
}

[[nodiscard]] bool ValidateMaterialGraphRuntimeDependencies(
    const kb::render::RenderMaterialGraphDocument& rootGraph,
    kb::assets::AssetManager& manager,
    const SourceSnapshotMap& snapshots,
    kb::render::RenderMaterialGraphFunctionLibrary& functionLibrary,
    std::unordered_map<std::uint64_t, kb::render::RenderMaterialParameterCollectionData>& collections,
    std::string& error) {
    functionLibrary.entries.clear();
    collections.clear();
    std::vector<std::uint64_t> pendingFunctions =
        kb::render::DiscoverRenderMaterialGraphFunctionDependencies(rootGraph);
    std::vector<std::uint64_t> collectionIds =
        kb::render::DiscoverRenderMaterialGraphParameterCollectionDependencies(rootGraph);
    std::unordered_set<std::uint64_t> visitedFunctions;
    std::unordered_set<std::uint64_t> discoveredCollections{
        collectionIds.begin(), collectionIds.end() };

    while (!pendingFunctions.empty()) {
        const std::uint64_t functionId = pendingFunctions.back();
        pendingFunctions.pop_back();
        if (functionId == 0U || !visitedFunctions.insert(functionId).second) {
            continue;
        }
        const kb::assets::AssetMetadata* metadata =
            manager.Registry().Find(kb::assets::AssetId{ functionId });
        if (metadata == nullptr || metadata->type != kb::render::kRenderMaterialFunctionAssetType ||
            !metadata->runtimeLoadable) {
            error = "material graph requires a missing, editor-only, or wrong-type function asset: " +
                std::to_string(functionId);
            return false;
        }
        std::vector<std::uint8_t> functionBytes;
        if (!ReadSourceSnapshot(snapshots, functionId, functionBytes, error)) {
            error = "material graph function is absent from the runtime closure: " +
                kb::assets::NormalizeAssetPath(metadata->virtualPath);
            return false;
        }
        kb::assets::AssetMemoryInputStream input{ functionBytes };
        const kb::render::RenderMaterialAssetParseResult parsed =
            kb::render::RenderMaterialFunctionAssetLoader::LoadFunctionWithDiagnostics(input);
        if (!parsed.Succeeded()) {
            error = "material graph function could not be parsed: " +
                kb::assets::NormalizeAssetPath(metadata->virtualPath) + "\n" + parsed.ErrorMessage();
            return false;
        }
        const std::uint64_t packagedContentHash = asset_bake::HashBakeBytes(functionBytes);
        functionLibrary.entries.push_back(kb::render::RenderMaterialGraphFunctionLibraryEntry{
            .assetId = metadata->id.value,
            .contentHash = packagedContentHash == 0U ? 1U : packagedContentHash,
            .name = metadata->virtualPath.generic_string(),
            .graph = parsed.asset->graph,
        });
        for (const std::uint64_t nestedId :
             kb::render::DiscoverRenderMaterialGraphFunctionDependencies(parsed.asset->graph)) {
            if (!visitedFunctions.contains(nestedId)) pendingFunctions.push_back(nestedId);
        }
        for (const std::uint64_t collectionId :
             kb::render::DiscoverRenderMaterialGraphParameterCollectionDependencies(parsed.asset->graph)) {
            if (collectionId != 0U && discoveredCollections.insert(collectionId).second) {
                collectionIds.push_back(collectionId);
            }
        }
    }

    for (const std::uint64_t collectionId : collectionIds) {
        if (collectionId == 0U) continue;
        const kb::assets::AssetMetadata* metadata =
            manager.Registry().Find(kb::assets::AssetId{ collectionId });
        if (metadata == nullptr ||
            metadata->type != kb::render::kRenderMaterialParameterCollectionAssetType ||
            !metadata->runtimeLoadable) {
            error = "material graph requires a missing, editor-only, or wrong-type parameter collection asset: " +
                std::to_string(collectionId);
            return false;
        }
        std::vector<std::uint8_t> collectionBytes;
        if (!ReadSourceSnapshot(snapshots, collectionId, collectionBytes, error)) {
            error = "material parameter collection is absent from the runtime closure: " +
                kb::assets::NormalizeAssetPath(metadata->virtualPath);
            return false;
        }
        kb::assets::AssetMemoryInputStream input{ collectionBytes };
        const kb::render::RenderMaterialParameterCollectionParseResult parsed =
            kb::render::RenderMaterialParameterCollectionAssetLoader::LoadCollectionWithDiagnostics(input);
        if (!parsed.Succeeded()) {
            error = "material parameter collection could not be parsed: " +
                kb::assets::NormalizeAssetPath(metadata->virtualPath) + "\n" + parsed.ErrorMessage();
            return false;
        }
        if (!collections.emplace(collectionId, std::move(*parsed.collection)).second) {
            error = "material graph dependency validation encountered a duplicate parameter collection: " +
                std::to_string(collectionId);
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint64_t NonZeroContentHash(std::span<const std::uint8_t> bytes) noexcept {
    const std::uint64_t hash = asset_bake::HashBakeBytes(bytes);
    return hash == 0U ? 1U : hash;
}

[[nodiscard]] asset_bake::AssetBakeKey MakeStoredBytesKey(
    std::span<const std::uint8_t> bytes,
    const asset_bake::BakeTargetProfile& profile,
    std::string_view bakerId,
    std::string_view settings) {
    const auto* settingsData = reinterpret_cast<const std::uint8_t*>(settings.data());
    asset_bake::AssetBakeKey key{};
    key.sourceContentHash = NonZeroContentHash(bytes);
    key.bakerId = std::string{ bakerId };
    key.bakerVersion = std::string{ kStoredBytesBakerVersion };
    key.targetProfileId = std::string{ profile.identifier };
    key.targetProfileHash = asset_bake::BakeTargetProfileFingerprint(profile);
    key.settingsHash = NonZeroContentHash({ settingsData, settings.size() });
    return key;
}

[[nodiscard]] bool StoreBytesArtifact(
    asset_bake::AssetPackWriter& writer,
    const asset_bake::BakeTargetProfile& profile,
    std::span<const std::uint8_t> bytes,
    std::string_view assetTypeId,
    std::string_view bakerId,
    std::string_view settings,
    asset_bake::AssetBakeDigest& digest,
    std::string& error) {
    if (bytes.empty()) {
        error = "internal cook error: an empty block reached the package writer";
        return false;
    }
    asset_bake::BakedAssetDescriptor descriptor{};
    descriptor.key = MakeStoredBytesKey(bytes, profile, bakerId, settings);
    descriptor.assetTypeId = std::string{ assetTypeId };
    const asset_bake::BakedAssetSinkStatus begin = writer.BeginAsset(descriptor);
    if (begin != asset_bake::BakedAssetSinkStatus::Success) {
        error = "package refused artifact begin: " + std::string{ asset_bake::ToString(begin) };
        return false;
    }
    const asset_bake::BakedAssetSinkStatus write =
        writer.WritePrimaryBlock(bytes, profile.packageBlockAlignmentBytes);
    if (write != asset_bake::BakedAssetSinkStatus::Success) {
        writer.AbortAsset();
        error = "package refused artifact bytes: " + std::string{ asset_bake::ToString(write) };
        return false;
    }
    const asset_bake::BakedAssetSinkStatus commit = writer.CommitAsset();
    if (commit != asset_bake::BakedAssetSinkStatus::Success) {
        writer.AbortAsset();
        error = "package refused artifact commit: " + std::string{ asset_bake::ToString(commit) };
        return false;
    }
    digest = descriptor.key.Digest();
    return true;
}

[[nodiscard]] bool StoreSourceFile(
    asset_bake::AssetPackWriter& writer,
    const asset_bake::BakeTargetProfile& profile,
    std::span<const std::uint8_t> sourceBytes,
    std::string_view settings,
    asset_bake::AssetBakeDigest& digest,
    std::string& error) {
    std::vector<std::uint8_t> sourceBlob;
    if (!asset_bake::EncodeRuntimeSourceBlob(sourceBytes, sourceBlob)) {
        error = "source file exceeds the package block budget";
        return false;
    }
    return StoreBytesArtifact(
        writer,
        profile,
        sourceBlob,
        asset_bake::kSourceAssetTypeId,
        kSourceFileBakerId,
        settings,
        digest,
        error);
}

[[nodiscard]] std::filesystem::path ResolveProjectFile(
    const std::filesystem::path& requested,
    std::string& error) {
    std::error_code pathError;
    std::filesystem::path path = std::filesystem::absolute(requested, pathError).lexically_normal();
    if (pathError) {
        error = "project path could not be resolved";
        return {};
    }
    if (std::filesystem::is_directory(path, pathError) && !pathError) {
        path /= "Project.21kbproject";
    }
    if (pathError || !std::filesystem::is_regular_file(path, pathError) || pathError) {
        error = "project descriptor was not found: " + path.generic_string();
        return {};
    }
    return path;
}

[[nodiscard]] bool PathIsWithin(
    const std::filesystem::path& child,
    const std::filesystem::path& parent) {
    std::error_code childError;
    std::error_code parentError;
    const std::filesystem::path absoluteChild = std::filesystem::weakly_canonical(child, childError);
    const std::filesystem::path absoluteParent = std::filesystem::weakly_canonical(parent, parentError);
    if (childError || parentError) {
        return false;
    }
    const std::filesystem::path relative = absoluteChild.lexically_relative(absoluteParent);
    return !relative.empty() && *relative.begin() != "..";
}

[[nodiscard]] std::optional<kb::render::RenderMaterialGraphShaderBackend> MaterialBackend(
    asset_bake::ShaderBakeBackend backend) noexcept {
    using GraphBackend = kb::render::RenderMaterialGraphShaderBackend;
    switch (backend) {
    case asset_bake::ShaderBakeBackend::Dxbc: return GraphBackend::Dxbc;
    case asset_bake::ShaderBakeBackend::Dxil: return GraphBackend::Dxil;
    case asset_bake::ShaderBakeBackend::Spirv: return GraphBackend::Spirv;
    case asset_bake::ShaderBakeBackend::Glsl: return GraphBackend::Glsl;
    case asset_bake::ShaderBakeBackend::Essl: return GraphBackend::Essl;
    case asset_bake::ShaderBakeBackend::Metal: return GraphBackend::Metal;
    case asset_bake::ShaderBakeBackend::Wgsl: return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] bool EncodeTextureSourcePng(
    const kb::render::RenderTextureAssetData& texture,
    std::vector<std::uint8_t>& bytes) {
    const std::size_t expected = static_cast<std::size_t>(texture.width) * texture.height * 4U;
    if (texture.width == 0U || texture.height == 0U || texture.rgba8.size() < expected) {
        return false;
    }
    bytes.clear();
    VectorWriter writer{ bytes };
    bx::Error error;
    bimg::imageWritePng(
        &writer,
        texture.width,
        texture.height,
        static_cast<std::uint32_t>(texture.width) * 4U,
        texture.rgba8.data(),
        bimg::TextureFormat::RGBA8,
        false,
        &error);
    return error.isOk() && !bytes.empty();
}

[[nodiscard]] bool TextureBakeSource(
    const kb::assets::AssetMetadata& metadata,
    const std::vector<std::uint8_t>& fileBytes,
    const kb::render::RenderTextureAssetData& decoded,
    std::vector<std::uint8_t>& sourceBytes,
    std::string& error) {
    std::string extension = metadata.physicalPath.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (extension == ".21kb" && metadata.importCategory == "Texture") {
        kb::assets::ImportedAssetLoader loader;
        kb::assets::AssetLoadResult loaded = loader.Load(kb::assets::AssetLoadRequest{
            .metadata = metadata,
            .resolvedPath = metadata.physicalPath,
            .sourceBytes = std::span<const std::uint8_t>{ fileBytes },
        });
        if (!loaded.Succeeded()) {
            error = "imported texture container could not be decoded: " + metadata.virtualPath.generic_string();
            return false;
        }
        const std::shared_ptr<kb::assets::ImportedAsset> imported =
            std::static_pointer_cast<kb::assets::ImportedAsset>(loaded.asset);
        if (imported == nullptr || imported->payload.empty()) {
            error = "imported texture container has no payload: " + metadata.virtualPath.generic_string();
            return false;
        }
        sourceBytes.resize(imported->payload.size());
        std::memcpy(sourceBytes.data(), imported->payload.data(), imported->payload.size());
        return true;
    }
    if (extension == ".kbtex") {
        if (!EncodeTextureSourcePng(decoded, sourceBytes)) {
            error = "texture authoring data could not be canonicalized: " + metadata.virtualPath.generic_string();
            return false;
        }
        return true;
    }
    sourceBytes = fileBytes;
    return true;
}

[[nodiscard]] bool IsTextureAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderTexture" || metadata.importCategory == "Texture";
}

[[nodiscard]] bool IsMeshAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMesh";
}

[[nodiscard]] bool IsMaterialAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterial";
}

[[nodiscard]] bool CollectRuntimeAssetClosure(
    const kb::assets::AssetManager& manager,
    const kb::project::ProjectSettings& settings,
    std::vector<kb::assets::AssetMetadata>& assets,
    std::string& error) {
    std::vector<kb::assets::AssetId> pending;
    const auto addRoot = [&](std::string_view label,
                             const std::string& virtualPath,
                             bool required,
                             std::string_view expectedType) {
        if (virtualPath.empty()) {
            if (required) {
                error = std::string{ label } + " is not configured";
                return false;
            }
            return true;
        }
        const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(virtualPath);
        if (metadata == nullptr) {
            error = std::string{ label } + " was not discovered as an asset: " + virtualPath;
            return false;
        }
        if (!expectedType.empty() && metadata->type != expectedType) {
            error = std::string{ label } + " must reference a " + std::string{ expectedType } +
                " asset, but references " + metadata->type + ": " + virtualPath;
            return false;
        }
        if (!metadata->runtimeLoadable) {
            error = std::string{ label } + " is editor-only: " + virtualPath;
            return false;
        }
        const kb::assets::AssetCompatibilityReport compatibility = manager.ValidateCompatibility(metadata->id);
        if (!compatibility.compatible) {
            error = std::string{ label } + " is not runtime-compatible\n" + compatibility.FormatDiagnostics();
            return false;
        }
        pending.push_back(metadata->id);
        return true;
    };

    if (!addRoot("default map", settings.defaultMap, true, "Scene") ||
        (settings.inputEnabled &&
            !addRoot("input mapping context", settings.inputMappingContext, false, "InputMappingContext")) ||
        !addRoot("physics layers asset", settings.physicsLayersAsset, false, "PhysicsLayers")) {
        return false;
    }

    std::unordered_set<std::uint64_t> visited;
    while (!pending.empty()) {
        const kb::assets::AssetId id = pending.back();
        pending.pop_back();
        if (!visited.insert(id.value).second) {
            continue;
        }
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
        if (metadata == nullptr) {
            error = "runtime dependency disappeared from the asset registry: " + kb::assets::ToString(id);
            return false;
        }
        if (!metadata->runtimeLoadable) {
            error = "runtime dependency is editor-only: " + kb::assets::NormalizeAssetPath(metadata->virtualPath);
            return false;
        }
        assets.push_back(*metadata);
        pending.insert(pending.end(), metadata->dependencies.begin(), metadata->dependencies.end());
    }

    std::ranges::sort(assets, [](const kb::assets::AssetMetadata& lhs, const kb::assets::AssetMetadata& rhs) {
        return kb::assets::NormalizeAssetPath(lhs.virtualPath) < kb::assets::NormalizeAssetPath(rhs.virtualPath);
    });
    return true;
}

[[nodiscard]] bool CookGraphShaders(
    const ProjectCookRequest& request,
    const CookerToolInputSnapshot& toolInputs,
    const asset_bake::BakeTargetProfile& profile,
    const kb::assets::AssetMetadata& metadata,
    const kb::render::RenderMaterialAssetData& material,
    kb::assets::AssetManager& manager,
    const SourceSnapshotMap& snapshots,
    asset_bake::AssetPackWriter& writer,
    std::vector<asset_bake::RuntimeArtifactReference>& references,
    std::size_t& shaderArtifactCount,
    std::string& error) {
    kb::render::RenderMaterialGraphDocument graph = material.graph;
    if (material.graphSourceAssetId != 0U || !material.graphSourceAssetPath.empty()) {
        const kb::assets::AssetMetadata* graphMetadata =
            kb::render::ResolveRenderMaterialSourceGraphMetadata(
                manager.Registry(), metadata, material);
        if (graphMetadata == nullptr) {
            error = "authoritative material sourceGraph could not be resolved: " +
                metadata.virtualPath.generic_string();
            return false;
        }
        std::vector<std::uint8_t> graphBytes;
        if (!ReadSourceSnapshot(snapshots, graphMetadata->id.value, graphBytes, error)) {
            error = "authoritative material sourceGraph is absent from the runtime closure: " +
                kb::assets::NormalizeAssetPath(graphMetadata->virtualPath);
            return false;
        }
        kb::assets::AssetMemoryInputStream graphInput{ graphBytes };
        kb::render::RenderMaterialAssetParseResult sourceGraph =
            kb::render::RenderMaterialGraphAssetLoader::LoadGraphWithDiagnostics(graphInput);
        if (!sourceGraph.Succeeded()) {
            error = "authoritative material sourceGraph could not be parsed: " +
                kb::assets::NormalizeAssetPath(graphMetadata->virtualPath);
            const std::string details = sourceGraph.ErrorMessage();
            if (!details.empty()) error += "\n" + details;
            return false;
        }
        graph = std::move(sourceGraph.asset->graph);
    }
    kb::render::RenderMaterialAssetData materialForCook = material;
    materialForCook.graph = std::move(graph);
    kb::render::RenderMaterialGraphFunctionLibrary functionLibrary;
    std::unordered_map<std::uint64_t, kb::render::RenderMaterialParameterCollectionData> collections;
    if (!ValidateMaterialGraphRuntimeDependencies(
            materialForCook.graph, manager, snapshots, functionLibrary, collections, error)) {
        error = "material graph dependency validation failed for " +
            metadata.virtualPath.generic_string() + "\n" + error;
        return false;
    }
    const kb::render::RenderMaterialCookPayload payload =
        kb::render::RenderMaterialCookPayloadBuilder::Build(
            materialForCook, metadata, manager.Registry(), functionLibrary);
    if (!payload.graphBacked) {
        return true;
    }
    if (!payload.graphCompileSucceeded) {
        error = "material graph compilation failed: " + metadata.virtualPath.generic_string();
        for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : payload.graphDiagnostics) {
            if (!diagnostic.message.empty()) {
                error += "\n  " + diagnostic.message;
            }
        }
        return false;
    }
    for (const kb::render::RenderMaterialGraphReflectionUniform& uniform :
         payload.graphShader.reflection.uniforms) {
        if (uniform.source !=
            kb::render::RenderMaterialGraphReflectionUniformSource::ParameterCollection) {
            continue;
        }
        const auto collection = collections.find(uniform.collectionAssetId);
        const bool parameterExists = collection != collections.end() && std::ranges::any_of(
            collection->second.parameters,
            [&](const kb::render::RenderMaterialParameterCollectionParameter& parameter) {
                return parameter.stableId == uniform.collectionParameterStableId;
            });
        if (!parameterExists) {
            const kb::assets::AssetMetadata* collectionMetadata =
                manager.Registry().Find(kb::assets::AssetId{ uniform.collectionAssetId });
            error = "material graph requires missing parameter '" +
                uniform.collectionParameterStableId + "' from parameter collection " +
                (collectionMetadata != nullptr
                    ? kb::assets::NormalizeAssetPath(collectionMetadata->virtualPath)
                    : std::to_string(uniform.collectionAssetId));
            return false;
        }
    }
    if (request.shadercPath.empty() || !std::filesystem::is_regular_file(request.shadercPath)) {
        error = "material graph requires shaderc, but the compiler was not found";
        return false;
    }
    const std::filesystem::path& shaderSources = toolInputs.shaderSourceRoot;
    if (!std::filesystem::is_regular_file(shaderSources / "varying.def.sc")) {
        error = "renderer shader sources were not found under engine root";
        return false;
    }

    std::vector<kb::render::RenderMaterialGraphShaderBackend> backends;
    for (std::uint32_t index = 0U; index < asset_bake::kShaderBakeBackendCount; ++index) {
        const auto backend = static_cast<asset_bake::ShaderBakeBackend>(index);
        if (!asset_bake::HasShaderBakeBackend(profile.shaderBackends, backend)) {
            continue;
        }
        const std::optional<kb::render::RenderMaterialGraphShaderBackend> mapped = MaterialBackend(backend);
        if (!mapped.has_value()) {
            error = "material graph cooker has no production compiler for backend: " +
                std::string{ asset_bake::ShaderBakeBackendName(backend) };
            return false;
        }
        backends.push_back(*mapped);
    }

    for (const std::string_view pass : kMaterialPasses) {
        kb::render::RenderMaterialGraphShaderArtifactRequest shaderRequest{};
        shaderRequest.shadercPath = request.shadercPath.generic_string();
        shaderRequest.varyingDefPath = (shaderSources / "varying.def.sc").generic_string();
        shaderRequest.includeDirs = {
            shaderSources.generic_string(),
            toolInputs.bgfxIncludeRoot.generic_string(),
        };
        shaderRequest.dependencyFiles = {
            (shaderSources / "pbr_graph_forward.sh").generic_string(),
            (shaderSources / "gbuffer_contract.sh").generic_string(),
        };
        shaderRequest.cacheRoot = request.cacheRoot.generic_string();
        shaderRequest.pass = std::string{ pass };
        shaderRequest.shaderPlatform = profile.shaderPlatform;
        shaderRequest.materialTypeVersion = material.materialTypeVersion;
        const kb::render::RenderMaterialGraphShaderArtifactResult cooked =
            kb::render::CookRenderMaterialGraphShaderArtifact(payload.graphShader, backends, shaderRequest);
        if (!cooked.Succeeded()) {
            error = "material shader cook failed: " + metadata.virtualPath.generic_string() +
                " pass=" + std::string{ pass };
            for (const kb::render::RenderMaterialGraphDiagnostic& diagnostic : cooked.diagnostics) {
                if (!diagnostic.message.empty()) {
                    error += "\n  " + diagnostic.message;
                }
            }
            return false;
        }
        const auto storeBinaries = [&](std::span<const kb::render::RenderMaterialGraphShaderBinary> binaries,
                                       std::string_view stage) {
            for (const kb::render::RenderMaterialGraphShaderBinary& binary : binaries) {
                std::vector<std::uint8_t> bytes;
                if (!ReadFileBytes(binary.binaryPath, bytes, error) || bytes.empty()) {
                    if (error.empty()) {
                        error = "material shader compiler produced an empty binary";
                    }
                    return false;
                }
                const std::string qualifier = std::to_string(cooked.artifact->graphSourceHash) + ":" +
                    std::to_string(cooked.artifact->variantKey) + ":" + std::string{ pass } + ":" +
                    std::string{ kb::render::RenderMaterialGraphShaderBackendName(binary.backend) } + ":" +
                    std::string{ asset_bake::ShaderBakePlatformName(profile.shaderPlatform) } + ":" +
                    std::string{ stage };
                asset_bake::AssetBakeDigest digest{};
                if (!StoreBytesArtifact(
                        writer,
                        profile,
                        bytes,
                        asset_bake::kMaterialShaderAssetTypeId,
                        kShaderBinaryBakerId,
                        qualifier,
                        digest,
                        error)) {
                    return false;
                }
                references.push_back(asset_bake::RuntimeArtifactReference{
                    .digest = digest,
                    .encoding = asset_bake::RuntimeArtifactEncoding::MaterialShader,
                    .qualifier = qualifier,
                });
                ++shaderArtifactCount;
            }
            return true;
        };
        if (!storeBinaries(cooked.artifact->binaries, "fragment") ||
            (cooked.artifact->hasVertexShader && !storeBinaries(cooked.artifact->vertexBinaries, "vertex"))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string_view FixedShaderProfile(asset_bake::ShaderBakeBackend backend) noexcept {
    switch (backend) {
    case asset_bake::ShaderBakeBackend::Dxbc: return "s_5_0";
    case asset_bake::ShaderBakeBackend::Dxil: return "s_6_0";
    case asset_bake::ShaderBakeBackend::Spirv: return "spirv";
    case asset_bake::ShaderBakeBackend::Glsl: return "440";
    case asset_bake::ShaderBakeBackend::Essl: return "300_es";
    case asset_bake::ShaderBakeBackend::Metal: return "metal";
    case asset_bake::ShaderBakeBackend::Wgsl: break;
    }
    return {};
}

[[nodiscard]] std::string_view FixedShaderStageName(kb::render::ShaderStage stage) noexcept {
    switch (stage) {
    case kb::render::ShaderStage::Vertex: return "vertex";
    case kb::render::ShaderStage::Fragment: return "fragment";
    case kb::render::ShaderStage::Compute: return "compute";
    }
    return {};
}

#if defined(_WIN32)
[[nodiscard]] std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), wide.data(), count) != count) {
        return {};
    }
    return wide;
}

[[nodiscard]] std::wstring QuoteWindowsArgument(std::wstring_view value) {
    std::wstring quoted{ L"\"" };
    std::size_t backslashes = 0U;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            quoted.append(backslashes * 2U + 1U, L'\\');
            quoted.push_back(character);
            backslashes = 0U;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0U;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2U, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}
#endif

[[nodiscard]] int RunCookerTool(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    const std::filesystem::path& diagnosticPath) {
#if defined(_WIN32)
    const std::wstring executableWide = executable.wstring();
    std::wstring command = QuoteWindowsArgument(executableWide);
    for (const std::string& argument : arguments) {
        const std::wstring wide = Utf8ToWide(argument);
        if (!argument.empty() && wide.empty()) {
            return -1;
        }
        command.push_back(L' ');
        command += QuoteWindowsArgument(wide);
    }
    SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    const HANDLE diagnostic = CreateFileW(diagnosticPath.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (diagnostic == INVALID_HANDLE_VALUE) {
        return -1;
    }
    const HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = nullInput == INVALID_HANDLE_VALUE ? nullptr : nullInput;
    startup.hStdOutput = diagnostic;
    startup.hStdError = diagnostic;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(executableWide.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(diagnostic);
    if (nullInput != INVALID_HANDLE_VALUE) {
        CloseHandle(nullInput);
    }
    if (created == FALSE) {
        return -1;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = static_cast<DWORD>(-1);
    static_cast<void>(GetExitCodeProcess(process.hProcess, &exitCode));
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
#else
    posix_spawn_file_actions_t actions{};
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return -1;
    }
    const std::string diagnostic = diagnosticPath.string();
    static_cast<void>(posix_spawn_file_actions_addopen(
        &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0));
    static_cast<void>(posix_spawn_file_actions_addopen(
        &actions, STDOUT_FILENO, diagnostic.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600));
    static_cast<void>(posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO));
    const std::string executableString = executable.string();
    std::vector<std::string> ownedArguments;
    ownedArguments.reserve(arguments.size() + 1U);
    ownedArguments.push_back(executableString);
    ownedArguments.insert(ownedArguments.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    argv.reserve(ownedArguments.size() + 1U);
    for (std::string& argument : ownedArguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    pid_t process = 0;
    const int spawned = posix_spawn(&process, executableString.c_str(), &actions, nullptr, argv.data(), environ);
    static_cast<void>(posix_spawn_file_actions_destroy(&actions));
    if (spawned != 0) {
        return -1;
    }
    int status = 0;
    if (waitpid(process, &status, 0) < 0 || !WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
#endif
}

[[nodiscard]] std::string ReadCookerDiagnostic(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        return {};
    }
    constexpr std::size_t kMaxDiagnosticBytes = 16U * 1024U;
    std::string text(kMaxDiagnosticBytes, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(input.gcount()));
    return text;
}

[[nodiscard]] std::filesystem::path CreateFixedShaderWorkDirectory(
    const std::filesystem::path& cacheRoot,
    std::string_view shaderPlatform,
    std::string& error) {
    const std::filesystem::path parent = cacheRoot / "fixed-shader-work" / shaderPlatform;
    std::error_code directoryError;
    std::filesystem::create_directories(parent, directoryError);
    if (directoryError) {
        error = "fixed shader work directory could not be created: " + parent.generic_string();
        return {};
    }
#if defined(_WIN32)
    const std::uint64_t processId = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const std::uint64_t processId = static_cast<std::uint64_t>(getpid());
#endif
    static std::atomic<std::uint64_t> nextDirectory{ 0U };
    for (std::uint32_t attempt = 0U; attempt < 256U; ++attempt) {
        const std::uint64_t sequence = nextDirectory.fetch_add(1U, std::memory_order_relaxed);
        const std::filesystem::path candidate =
            parent / (std::to_string(processId) + "-" + std::to_string(sequence));
        directoryError.clear();
        if (std::filesystem::create_directory(candidate, directoryError)) {
            return candidate;
        }
        if (directoryError) {
            error = "fixed shader work directory could not be created: " + candidate.generic_string();
            return {};
        }
    }
    error = "a unique fixed shader work directory could not be allocated";
    return {};
}

[[nodiscard]] bool AddFixedShaders(
    const ProjectCookRequest& request,
    const CookerToolInputSnapshot& toolInputs,
    const asset_bake::BakeTargetProfile& profile,
    asset_bake::AssetPackWriter& writer,
    asset_bake::RuntimeAssetManifest& manifest,
    std::set<std::string>& packagedPaths,
    std::string& error) {
    if (request.shadercPath.empty() || !std::filesystem::is_regular_file(request.shadercPath)) {
        error = "the pinned renderer shader compiler was not found";
        return false;
    }
    const std::filesystem::path& sourceRoot = toolInputs.shaderSourceRoot;
    const std::filesystem::path& bgfxInclude = toolInputs.bgfxIncludeRoot;
    const std::filesystem::path varyingDef = sourceRoot / "varying.def.sc";
    if (!std::filesystem::is_directory(sourceRoot) || !std::filesystem::is_directory(bgfxInclude) ||
        !std::filesystem::is_regular_file(varyingDef)) {
        error = "renderer shader sources or pinned bgfx includes were not found";
        return false;
    }
    const std::string shaderPlatform{ asset_bake::ShaderBakePlatformName(profile.shaderPlatform) };
    const std::filesystem::path targetRoot =
        CreateFixedShaderWorkDirectory(request.cacheRoot, shaderPlatform, error);
    if (targetRoot.empty()) {
        return false;
    }
    const ScopedCookDirectory workDirectoryCleanup{ targetRoot };
    for (std::uint32_t index = 0U; index < asset_bake::kShaderBakeBackendCount; ++index) {
        const auto backend = static_cast<asset_bake::ShaderBakeBackend>(index);
        if (!asset_bake::HasShaderBakeBackend(profile.shaderBackends, backend)) {
            continue;
        }
        const std::string backendName{ asset_bake::ShaderBakeBackendName(backend) };
        const std::filesystem::path profileRoot = targetRoot / backendName;
        const std::string profileName{ FixedShaderProfile(backend) };
        if (profileName.empty()) {
            error = "no production shaderc profile exists for backend " + backendName;
            return false;
        }
        const kb::render::ShaderRuntimeFeatureMask requiredShaderFeatures =
            kb::render::PackagedGameShaderFeatures(backend);
        const std::vector<std::string_view> requiredPackagedShaders =
            kb::render::RequiredPackagedShaderNames(requiredShaderFeatures);
        std::error_code directoryError;
        std::filesystem::create_directories(profileRoot, directoryError);
        if (directoryError) {
            error = "fixed shader cache directory could not be created: " + profileRoot.generic_string();
            return false;
        }
        std::vector<const kb::render::ShaderManifestEntry*> compiledShaders;
        for (const kb::render::ShaderManifestEntry& shader : kb::render::RequiredShaderManifest()) {
            const bool requiredForPackage =
                std::ranges::find(requiredPackagedShaders, std::string_view{ shader.name }) !=
                requiredPackagedShaders.end();
            const bool selectedFeature = shader.requiredFeature != 0U &&
                (requiredShaderFeatures & shader.requiredFeature) != 0U;
            if (shader.requiredFeature != 0U && !selectedFeature) {
                continue;
            }
            if (shader.stage == kb::render::ShaderStage::Compute && !selectedFeature) {
                continue;
            }
            const std::filesystem::path sourcePath = sourceRoot / shader.name;
            if (!std::filesystem::is_regular_file(sourcePath)) {
                if (!requiredForPackage) {
                    continue;
                }
                error = "fixed renderer shader source is missing: " + sourcePath.generic_string();
                return false;
            }
            const std::filesystem::path binaryPath = profileRoot / (std::string{ shader.name } + ".bin");
            const std::filesystem::path diagnosticPath = profileRoot / (std::string{ shader.name } + ".log");
            const std::vector<std::string> arguments{
                "--type", std::string{ FixedShaderStageName(shader.stage) },
                "--platform", shaderPlatform,
                "--profile", profileName,
                "-f", sourcePath.generic_string(),
                "-o", binaryPath.generic_string(),
                "--varyingdef", varyingDef.generic_string(),
                "-i", sourceRoot.generic_string(),
                "-i", bgfxInclude.generic_string(),
                "-O", "3",
            };
            std::filesystem::remove(binaryPath, directoryError);
            directoryError.clear();
            if (!VerifyCookerExecutablesUnchanged(toolInputs, error)) {
                return false;
            }
            const int exitCode = RunCookerTool(request.shadercPath, arguments, diagnosticPath);
            if (!VerifyCookerExecutablesUnchanged(toolInputs, error)) {
                std::filesystem::remove(binaryPath, directoryError);
                return false;
            }
            const bool produced = exitCode == 0 && std::filesystem::is_regular_file(binaryPath) &&
                std::filesystem::file_size(binaryPath, directoryError) > 0U && !directoryError;
            if (!produced) {
                error = "fixed shader compilation failed for " + backendName + "/" + shader.name +
                    " (exit " + std::to_string(exitCode) + ")";
                const std::string diagnostic = ReadCookerDiagnostic(diagnosticPath);
                if (!diagnostic.empty()) {
                    error += "\n" + diagnostic;
                }
                return false;
            }
            std::filesystem::remove(diagnosticPath, directoryError);
            directoryError.clear();
            compiledShaders.push_back(&shader);
        }
        const kb::render::ShaderManifestValidationResult validation =
            kb::render::ValidatePackagedShaderManifestProfile(
                profileRoot, requiredShaderFeatures);
        if (!validation.Succeeded()) {
            error = "required renderer shaders are missing for backend " + backendName;
            for (const std::string& missing : validation.missingRequiredShaders) {
                error += "\n  " + missing;
            }
            return false;
        }
        for (const kb::render::ShaderManifestEntry* shader : compiledShaders) {
            const std::filesystem::path binaryPath = profileRoot / (std::string{ shader->name } + ".bin");
            std::vector<std::uint8_t> bytes;
            if (!ReadFileBytes(binaryPath, bytes, error) || bytes.empty()) {
                if (error.empty()) {
                    error = "renderer shader binary is empty: " + binaryPath.generic_string();
                }
                return false;
            }
            const std::string virtualPath =
                "/Engine/Shaders/" + shaderPlatform + "/" + backendName + "/" +
                binaryPath.filename().generic_string();
            asset_bake::AssetBakeDigest digest{};
            if (!StoreSourceFile(writer, profile, bytes, virtualPath, digest, error)) {
                return false;
            }
            if (!packagedPaths.insert(virtualPath).second) {
                error = "two renderer shaders claim one package path: " + virtualPath;
                return false;
            }
            manifest.auxiliaryFiles.push_back(asset_bake::RuntimeAuxiliaryFileEntry{
                .virtualPath = virtualPath,
                .contentHash = NonZeroContentHash(bytes),
                .artifactDigest = digest,
            });
        }
    }
    return true;
}

} // namespace

ProjectCookResult CookProject(const ProjectCookRequest& input, std::ostream& diagnostics) {
    ProjectCookRequest request = input;
    if (request.shadercPath.empty()) {
        request.shadercPath = std::filesystem::path{ KB_COOKER_DEFAULT_SHADERC };
    }
    if (request.engineRoot.empty()) {
        request.engineRoot = std::filesystem::path{ KB_COOKER_DEFAULT_ENGINE_ROOT };
    }
    std::string error;
    const std::filesystem::path projectFile = ResolveProjectFile(request.projectPath, error);
    if (projectFile.empty()) {
        return Failure(std::move(error));
    }

    asset_bake::BakeTargetProfile profile{};
    if (!asset_bake::TryFindBakeTargetProfile(request.targetProfileId, profile)) {
        return Failure("unknown target profile: " + request.targetProfileId);
    }
    const std::filesystem::path projectRoot = projectFile.parent_path();
    const std::filesystem::path settingsPath =
        kb::project::ProjectSettingsStore::FilePath(projectRoot);
    std::filesystem::path projectMetaPath = projectFile;
    projectMetaPath.replace_extension(".meta");
    CookConfigurationSnapshot configurationSnapshot{};
    if (!CaptureCookConfigurationInput(projectFile, true, configurationSnapshot[0], error) ||
        !CaptureCookConfigurationInput(projectMetaPath, true, configurationSnapshot[1], error) ||
        !CaptureCookConfigurationInput(settingsPath, false, configurationSnapshot[2], error)) {
        return Failure(std::move(error));
    }
    const kb::project::ProjectDescriptorReadResult project = kb::project::ProjectManager::LoadProject(projectFile);
    if (!project.succeeded) {
        return Failure("project descriptor load failed: " + project.error);
    }
    if (!VerifyCookConfigurationInputsUnchanged(configurationSnapshot, error)) {
        return Failure(std::move(error));
    }
    if (const std::optional<std::string_view> unsupported =
            kb::game::FirstUnsupportedPackagedRuntimeModule(profile.identifier, project.descriptor);
        unsupported.has_value()) {
        return Failure(
            "target " + std::string{ profile.identifier } +
            " cannot ship configured runtime module: " + std::string{ *unsupported });
    }
    const kb::project::ParticleProjectPolicyResult particlePolicy =
        kb::project::ParticleProjectPolicy::Inspect(projectRoot, project.descriptor);
    if (!particlePolicy.IsRunnable()) {
        return Failure(particlePolicy.diagnostic);
    }
    const kb::project::ProjectSettingsLoadResult settingsLoad =
        kb::project::ProjectSettingsStore::Load(settingsPath);
    if (!settingsLoad.Succeeded()) {
        return Failure("project settings load failed: " + settingsLoad.error);
    }
    if (!VerifyCookConfigurationInputsUnchanged(configurationSnapshot, error)) {
        return Failure(std::move(error));
    }
    const kb::project::ProjectSettings settings = settingsLoad.found
        ? settingsLoad.settings
        : kb::project::ProjectSettingsStore::FromLegacy(project.legacySettings, projectFile);
    if (settings.defaultMap.empty()) {
        return Failure("project has no default map");
    }

    const std::filesystem::path contentRoot =
        (projectRoot / std::filesystem::path{ project.descriptor.contentRoot }).lexically_normal();
    if (project.descriptor.contentRoot.empty() || std::filesystem::path{ project.descriptor.contentRoot }.is_absolute() ||
        !PathIsWithin(contentRoot, projectRoot) || !std::filesystem::is_directory(contentRoot)) {
        return Failure("project content root is invalid or unavailable");
    }
    if (PathIsWithin(request.outputPackPath, contentRoot)) {
        return Failure("output package must not be written inside the project's content root");
    }
    if (request.cacheRoot.empty()) {
        request.cacheRoot = request.outputPackPath.parent_path() / ".kb-cook-cache" / request.targetProfileId;
    }
    const std::filesystem::path toolSnapshotRoot =
        CreateCookSnapshotDirectory(request.cacheRoot, "shader-input-snapshots", error);
    if (toolSnapshotRoot.empty()) {
        return Failure(std::move(error));
    }
    const ScopedCookDirectory toolSnapshotCleanup{ toolSnapshotRoot };
    CookerToolInputSnapshot cookerToolInputs;
    if (!CaptureCookerToolInputs(request, toolSnapshotRoot, cookerToolInputs, error)) {
        return Failure(std::move(error));
    }

    // Cooking needs the canonical loader registry, not a running gameplay
    // world. Starting project modules here can execute scene systems while the
    // scheduler is attaching them and also makes cook output depend on host
    // runtime state. PrefabPrivate installs the same engine asset loaders but
    // deliberately omits modules and simulation systems.
    kb::scene::Scene scene{ kb::scene::SceneMode::PrefabPrivate };
    kb::render::RuntimeRenderAssetDiscovery renderDiscovery;
    renderDiscovery.SetDiscoveryEnabled(false);
    renderDiscovery.Ensure(scene, 0U);
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    if (!manager.HasLoaderForType("RenderMaterialParameterCollection")) {
        static_cast<void>(manager.RegisterLoader(
            std::make_unique<kb::render::RenderMaterialParameterCollectionAssetLoader>()));
    }
    if (!manager.Mounts().Mount("Game", contentRoot)) {
        return Failure("project content root could not be mounted");
    }
    const std::size_t discovered = manager.DiscoverMountedAssets();
    diagnostics << "discovered " << discovered << " project assets\n";
    std::vector<kb::assets::AssetMetadata> assets;
    if (!CollectRuntimeAssetClosure(manager, settings, assets, error)) {
        return Failure(std::move(error));
    }
    for (const kb::assets::AssetMetadata& metadata : assets) {
        std::string extension = metadata.physicalPath.extension().generic_string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        if (extension == asset_bake::kAssetPackFileExtension) {
            return Failure(
                "prebuilt target-specific package cannot be used as a source asset: " +
                kb::assets::NormalizeAssetPath(metadata.virtualPath));
        }
    }
    diagnostics << "selected " << assets.size() << " assets in the runtime dependency closure\n";
    const std::filesystem::path sourceSnapshotRoot =
        CreateCookSnapshotDirectory(request.cacheRoot, "source-snapshots", error);
    if (sourceSnapshotRoot.empty()) {
        return Failure(std::move(error));
    }
    const ScopedCookDirectory sourceSnapshotCleanup{ sourceSnapshotRoot };
    SourceSnapshotMap sourceSnapshots;
    if (!CaptureSourceSnapshots(assets, contentRoot, sourceSnapshotRoot, sourceSnapshots, error)) {
        return Failure(std::move(error));
    }

    ScopedCookOutputLock outputLock;
    if (!outputLock.Acquire(request.outputPackPath, error)) {
        return Failure(std::move(error));
    }
    const std::filesystem::path packCandidatePath =
        CreateCookPackCandidatePath(request.outputPackPath, error);
    if (packCandidatePath.empty()) {
        return Failure(std::move(error));
    }
    const ScopedCookPackCandidate packCandidateCleanup{ packCandidatePath };
    asset_bake::AssetPackWriter writer{ packCandidatePath, profile };
    asset_bake::RuntimeAssetManifest manifest{};
    manifest.targetProfileId = std::string{ profile.identifier };
    manifest.targetProfileHash = asset_bake::BakeTargetProfileFingerprint(profile);
    manifest.descriptor = project.descriptor;
    manifest.settings = settings;
    std::set<std::string> packagedPaths;
    ProjectCookResult result{};

    if (!AddFixedShaders(request, cookerToolInputs, profile, writer, manifest, packagedPaths, error)) {
        return Failure(std::move(error));
    }

    for (const kb::assets::AssetMetadata& metadata : assets) {
        const std::string virtualPath = kb::assets::NormalizeAssetPath(metadata.virtualPath);
        if (!packagedPaths.insert(virtualPath).second) {
            return Failure("two assets claim one package path: " + virtualPath);
        }
        const auto sourceSnapshot = sourceSnapshots.find(metadata.id.value);
        if (sourceSnapshot == sourceSnapshots.end()) {
            return Failure("asset source snapshot is missing: " + virtualPath);
        }
        std::vector<std::uint8_t> fileBytes;
        if (!ReadSourceSnapshot(sourceSnapshots, metadata.id.value, fileBytes, error)) {
            return Failure("asset source snapshot could not be read: " + virtualPath + "\n" + error);
        }

        asset_bake::RuntimeAssetManifestEntry entry{
            .id = metadata.id,
            .type = metadata.type,
            .importCategory = metadata.importCategory,
            .browseTag = metadata.browseTag,
            .name = metadata.name,
            .virtualPath = virtualPath,
            .sourceExtension = metadata.physicalPath.extension().generic_string(),
            // The runtime deployment verifies extracted bytes with the same target-independent
            // digest for every source, including assets whose editor registry used an older
            // filesystem hash. The value remains an opaque change token to AssetManager.
            .contentHash = NonZeroContentHash(fileBytes),
            .runtimeLoadable = metadata.runtimeLoadable,
            .dependencies = metadata.dependencies,
        };

        if (IsTextureAsset(metadata)) {
            kb::render::RenderTextureAssetLoader loader;
            const kb::assets::AssetLoadResult loaded = loader.Load(kb::assets::AssetLoadRequest{
                .metadata = metadata,
                .resolvedPath = sourceSnapshot->second.path,
                .sourceBytes = std::span<const std::uint8_t>{ fileBytes },
            });
            if (!loaded.Succeeded()) {
                return Failure("texture could not be decoded for cooking: " + virtualPath);
            }
            const std::shared_ptr<kb::render::RenderTextureAssetData> texture =
                std::static_pointer_cast<kb::render::RenderTextureAssetData>(loaded.asset);
            std::vector<std::uint8_t> textureSource;
            if (!TextureBakeSource(metadata, fileBytes, *texture, textureSource, error)) {
                return Failure(std::move(error));
            }
            const kb::render::bake::TextureBakeSettings textureSettings{
                .semantic = texture->semantic,
                .colorSpace = texture->colorSpace,
            };
            for (std::uint32_t familyIndex = 0U;
                 familyIndex < asset_bake::kTextureCompressionFamilyCount;
                 ++familyIndex) {
                const auto family = static_cast<asset_bake::TextureCompressionFamily>(familyIndex);
                if (!asset_bake::HasTextureCompressionFamily(profile.textureCompressions, family)) {
                    continue;
                }
                const render_bake::TextureBakeOutput baked =
                    render_bake::BakeTextureBytes(textureSource, textureSettings, profile, family, writer);
                if (baked.status != render_bake::TextureBakeStatus::Success) {
                    return Failure("texture cook failed for " + virtualPath + " family=" +
                        std::string{ asset_bake::TextureCompressionFamilyName(family) } + " status=" +
                        std::string{ render_bake::ToString(baked.status) } +
                        (baked.status == render_bake::TextureBakeStatus::SinkRejected
                                ? " sink=" + std::string{ asset_bake::ToString(baked.sinkStatus) }
                                : std::string{}));
                }
                entry.artifacts.push_back(asset_bake::RuntimeArtifactReference{
                    .digest = baked.key.Digest(),
                    .encoding = asset_bake::RuntimeArtifactEncoding::BakedTexture,
                    .qualifier = std::string{ asset_bake::TextureCompressionFamilyName(family) },
                });
                ++result.textureArtifactCount;
            }
        } else if (IsMeshAsset(metadata)) {
            kb::render::RenderMeshAssetLoader loader;
            const kb::assets::AssetLoadResult loaded = loader.Load(kb::assets::AssetLoadRequest{
                .metadata = metadata,
                .resolvedPath = sourceSnapshot->second.path,
                .sourceBytes = std::span<const std::uint8_t>{ fileBytes },
            });
            if (!loaded.Succeeded()) {
                return Failure("mesh import failed for cooking: " + virtualPath + "\n  " + loaded.error);
            }
            const std::shared_ptr<kb::render::RenderMeshAssetData> mesh =
                std::static_pointer_cast<kb::render::RenderMeshAssetData>(loaded.asset);
            const render_bake::MeshBakeOutput baked = render_bake::BakeMesh(*mesh, profile, writer);
            if (baked.status != render_bake::MeshBakeStatus::Success) {
                return Failure("mesh cook failed for " + virtualPath + " status=" +
                    std::string{ render_bake::ToString(baked.status) } +
                    (baked.status == render_bake::MeshBakeStatus::SinkRejected
                            ? " sink=" + std::string{ asset_bake::ToString(baked.sinkStatus) }
                            : std::string{}));
            }
            entry.artifacts.push_back(asset_bake::RuntimeArtifactReference{
                .digest = baked.key.Digest(),
                .encoding = asset_bake::RuntimeArtifactEncoding::BakedMesh,
            });
            ++result.meshArtifactCount;
        } else {
            asset_bake::AssetBakeDigest digest{};
            const std::string sourceSettings = metadata.type + ":" + entry.sourceExtension;
            if (!StoreSourceFile(writer, profile, fileBytes, sourceSettings, digest, error)) {
                return Failure(std::move(error));
            }
            entry.artifacts.push_back(asset_bake::RuntimeArtifactReference{
                .digest = digest,
                .encoding = asset_bake::RuntimeArtifactEncoding::SourceBytes,
            });
            if (IsMaterialAsset(metadata)) {
                kb::assets::AssetMemoryInputStream materialInput{ fileBytes };
                const kb::render::RenderMaterialAssetParseResult material =
                    kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(
                        materialInput,
                        kb::render::RenderMaterialAssetParseSourceContext{
                            .assetId = metadata.id,
                            .path = metadata.physicalPath,
                        });
                if (!material.Succeeded()) {
                    return Failure("material could not be parsed for cooking: " + virtualPath +
                        "\n" + material.ErrorMessage());
                }
                if (!CookGraphShaders(
                        request,
                        cookerToolInputs,
                        profile,
                        metadata,
                        *material.asset,
                        manager,
                        sourceSnapshots,
                        writer,
                        entry.artifacts,
                        result.shaderArtifactCount,
                        error)) {
                    return Failure(std::move(error));
                }
            }
        }
        if (!VerifySourceSnapshotIntegrity(sourceSnapshot->second, error)) {
            return Failure("asset source snapshot changed during import: " + virtualPath + "\n" + error);
        }
        manifest.assets.push_back(std::move(entry));
    }

    if (!VerifySnapshotSourcesUnchanged(assets, sourceSnapshots, error)) {
        return Failure(std::move(error));
    }
    if (!VerifyCookerToolInputsUnchanged(request, cookerToolInputs, error)) {
        return Failure(std::move(error));
    }

    std::vector<std::uint8_t> manifestBytes;
    const asset_bake::RuntimeAssetManifestStatus manifestStatus =
        asset_bake::EncodeRuntimeAssetManifest(manifest, manifestBytes);
    if (manifestStatus != asset_bake::RuntimeAssetManifestStatus::Success) {
        return Failure("runtime manifest could not be encoded: " +
            std::string{ asset_bake::ToString(manifestStatus) });
    }
    asset_bake::AssetBakeDigest manifestDigest{};
    if (!StoreBytesArtifact(
            writer,
            profile,
            manifestBytes,
            asset_bake::kRuntimeManifestAssetTypeId,
            kRuntimeManifestBakerId,
            profile.identifier,
            manifestDigest,
            error)) {
        return Failure(std::move(error));
    }
    static_cast<void>(manifestDigest);
    const asset_bake::BakedAssetSinkStatus finish = writer.Finish();
    if (finish != asset_bake::BakedAssetSinkStatus::Success) {
        return Failure("package publication failed: " + std::string{ asset_bake::ToString(finish) });
    }

    auto validationPack = std::make_shared<asset_bake::RuntimeAssetPack>();
    const asset_bake::RuntimeAssetPackStatus mountStatus =
        validationPack->Mount(writer.PackPath(), profile);
    if (mountStatus != asset_bake::RuntimeAssetPackStatus::Success) {
        return Failure("cooked package failed its runtime mount gate: " +
            std::string{ asset_bake::ToString(mountStatus) });
    }
    if (const std::optional<std::string_view> unsupported =
            kb::game::FirstUnsupportedPackagedRuntimeModule(
                profile.identifier, validationPack->Manifest().descriptor);
        unsupported.has_value()) {
        return Failure("cooked package contains a runtime module unsupported by target " +
            std::string{ profile.identifier } + ": " + std::string{ *unsupported });
    }
    const kb::render::RuntimeAssetPackValidationResult validation =
        kb::render::ValidateRuntimeAssetPack(validationPack, profile);
    if (!validation.Succeeded()) {
        return Failure("cooked package failed semantic runtime validation: " + validation.error);
    }
    validationPack.reset();
    // The runtime gate can be deliberately expensive (all assets, shader variants and
    // payloads). Recheck both authored inputs and cooker tools after it, immediately before
    // the one publication rename, so a save during validation cannot be reported as a
    // successful cook of the current project state.
    if (!VerifySnapshotSourcesUnchanged(assets, sourceSnapshots, error)) {
        return Failure(std::move(error));
    }
    if (!VerifyCookerToolInputsUnchanged(request, cookerToolInputs, error)) {
        return Failure(std::move(error));
    }
    if (!VerifyCookConfigurationInputsUnchanged(configurationSnapshot, error)) {
        return Failure(std::move(error));
    }
    const std::filesystem::path publicationPath =
        writer.PackPath().parent_path() / request.outputPackPath.filename();
    if (!PublishValidatedCookPack(writer.PackPath(), publicationPath)) {
        return Failure("validated package could not be published atomically");
    }

    result.succeeded = true;
    result.assetCount = manifest.assets.size();
    result.auxiliaryFileCount = manifest.auxiliaryFiles.size();
    return result;
}

} // namespace kb::game
