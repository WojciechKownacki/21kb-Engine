#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include "engine/assets/ImportedAsset.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "engine/assets/bake/AssetPackReader.hpp"
#include "kb/render/bake/TextureBaker.hpp"

#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>

#include <atomic>
#include <cstddef>
#include <charconv>
#include <cctype>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <istream>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_set>

namespace kb::render {
namespace {

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] std::string_view StripComment(std::string_view line) noexcept {
    const std::size_t comment = line.find('#');
    return comment == std::string_view::npos ? line : line.substr(0U, comment);
}

template <typename T>
[[nodiscard]] bool ParseUnsigned(std::string_view text, T& output) noexcept {
    text = Trim(text);
    std::uint32_t parsed = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed > static_cast<std::uint32_t>(std::numeric_limits<T>::max())) {
        return false;
    }
    output = static_cast<T>(parsed);
    return true;
}

[[nodiscard]] bool ParseSize(std::string_view rest, RenderTextureAssetData& asset) {
    std::istringstream stream{ std::string{ rest } };
    std::string width;
    std::string height;
    return (stream >> width >> height) &&
        ParseUnsigned(width, asset.width) &&
        ParseUnsigned(height, asset.height) &&
        asset.width > 0U &&
        asset.height > 0U;
}

[[nodiscard]] bool ParseDimension(std::string_view text, RenderTextureDimension& dimension) noexcept {
    text = Trim(text);
    if (text == "2d" || text == "Texture2D") {
        dimension = RenderTextureDimension::Texture2D;
        return true;
    }
    if (text == "cube" || text == "TextureCube") {
        dimension = RenderTextureDimension::TextureCube;
        return true;
    }
    if (text == "3d" || text == "volume" || text == "Texture3D") {
        dimension = RenderTextureDimension::Texture3D;
        return true;
    }
    if (text == "2dArray" || text == "array" || text == "Texture2DArray") {
        dimension = RenderTextureDimension::Texture2DArray;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseColorSpace(std::string_view text, RenderTextureAssetColorSpace& colorSpace) noexcept {
    text = Trim(text);
    if (text == "unknown" || text == "Unknown") {
        colorSpace = RenderTextureAssetColorSpace::Unknown;
        return true;
    }
    if (text == "linear" || text == "Linear") {
        colorSpace = RenderTextureAssetColorSpace::Linear;
        return true;
    }
    if (text == "srgb" || text == "sRGB" || text == "Srgb") {
        colorSpace = RenderTextureAssetColorSpace::Srgb;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseSemantic(std::string_view text, RenderTextureAssetSemantic& semantic) noexcept {
    text = Trim(text);
    if (text == "unknown" || text == "Unknown") {
        semantic = RenderTextureAssetSemantic::Unknown;
        return true;
    }
    if (text == "baseColor" || text == "BaseColor") {
        semantic = RenderTextureAssetSemantic::BaseColor;
        return true;
    }
    if (text == "normal" || text == "Normal") {
        semantic = RenderTextureAssetSemantic::Normal;
        return true;
    }
    if (text == "metallicRoughness" || text == "MetallicRoughness") {
        semantic = RenderTextureAssetSemantic::MetallicRoughness;
        return true;
    }
    if (text == "occlusion" || text == "Occlusion") {
        semantic = RenderTextureAssetSemantic::Occlusion;
        return true;
    }
    if (text == "emissive" || text == "Emissive") {
        semantic = RenderTextureAssetSemantic::Emissive;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseRgba8(std::string_view rest, std::uint8_t (&rgba)[4]) {
    std::istringstream stream{ std::string{ rest } };
    std::string r;
    std::string g;
    std::string b;
    std::string a;
    return (stream >> r >> g >> b >> a) &&
        ParseUnsigned(r, rgba[0]) &&
        ParseUnsigned(g, rgba[1]) &&
        ParseUnsigned(b, rgba[2]) &&
        ParseUnsigned(a, rgba[3]);
}

[[nodiscard]] std::optional<std::size_t> TextureTexelCount(const RenderTextureAssetData& asset) noexcept {
    std::size_t sliceCount = 1U;
    switch (asset.dimension) {
    case RenderTextureDimension::Texture2D:
        if (asset.depth != 1U || asset.layers != 1U) return std::nullopt;
        break;
    case RenderTextureDimension::TextureCube:
        if (asset.width != asset.height || asset.depth != 1U || asset.layers != 1U) return std::nullopt;
        sliceCount = 6U;
        break;
    case RenderTextureDimension::Texture3D:
        if (asset.depth <= 1U || asset.layers != 1U) return std::nullopt;
        sliceCount = asset.depth;
        break;
    case RenderTextureDimension::Texture2DArray:
        if (asset.depth != 1U || asset.layers <= 1U) return std::nullopt;
        sliceCount = asset.layers;
        break;
    }

    const std::size_t width = asset.width;
    const std::size_t height = asset.height;
    if (width == 0U || height == 0U || width > std::numeric_limits<std::size_t>::max() / height) {
        return std::nullopt;
    }
    const std::size_t sliceTexels = width * height;
    if (sliceTexels > std::numeric_limits<std::size_t>::max() / sliceCount) {
        return std::nullopt;
    }
    return sliceTexels * sliceCount;
}

[[nodiscard]] bool FillTexture(RenderTextureAssetData& asset, const std::uint8_t (&rgba)[4]) {
    const std::optional<std::size_t> texelCount = TextureTexelCount(asset);
    if (!texelCount.has_value() || *texelCount > std::numeric_limits<std::size_t>::max() / 4U) {
        return false;
    }
    asset.rgba8.resize(*texelCount * 4U);
    for (std::size_t index = 0U; index < asset.rgba8.size(); index += 4U) {
        asset.rgba8[index + 0U] = rgba[0];
        asset.rgba8[index + 1U] = rgba[1];
        asset.rgba8[index + 2U] = rgba[2];
        asset.rgba8[index + 3U] = rgba[3];
    }
    return true;
}

[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    for (char& character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return extension;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input) {
        return std::nullopt;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0) {
        return std::nullopt;
    }
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input) {
        return std::nullopt;
    }
    return bytes;
}

[[nodiscard]] std::optional<RenderTextureAssetData> LoadImageBytes(const void* data, std::size_t size) {
    if (data == nullptr || size == 0U || size > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return std::nullopt;
    }

    bx::DefaultAllocator allocator;
    // The error object is not optional in practice. Left null, bimg substitutes a temporary
    // guarded by a scope object that asserts on destruction that nothing set an error - and
    // refusing a file IS setting one, "Unrecognized image format." among them. A debug build
    // therefore breaks into the debugger on any texture it cannot read, so a single corrupt
    // or misnamed file in a project takes the editor down instead of failing one load.
    bx::Error parseError;
    bimg::ImageContainer* image = bimg::imageParse(
        &allocator,
        data,
        static_cast<std::uint32_t>(size),
        bimg::TextureFormat::RGBA8,
        &parseError);
    if (image == nullptr || !parseError.isOk()) {
        if (image != nullptr) {
            bimg::imageFree(image);
        }
        return std::nullopt;
    }

    RenderTextureAssetData asset{};
    if (image->m_width == 0U ||
        image->m_height == 0U ||
        image->m_depth == 0U ||
        image->m_numLayers == 0U ||
        image->m_numMips == 0U ||
        image->m_width > std::numeric_limits<std::uint16_t>::max() ||
        image->m_height > std::numeric_limits<std::uint16_t>::max() ||
        image->m_depth > std::numeric_limits<std::uint16_t>::max() ||
        image->m_data == nullptr ||
        image->m_size == 0U) {
        bimg::imageFree(image);
        return std::nullopt;
    }

    asset.width = static_cast<std::uint16_t>(image->m_width);
    asset.height = static_cast<std::uint16_t>(image->m_height);
    asset.depth = static_cast<std::uint16_t>(image->m_depth);
    asset.layers = image->m_numLayers;
    // The runtime raw-texture API accepts only a hasMips bit and therefore requires a complete
    // chain. Preserve the legacy loader contract (LOD0 only), but collect LOD0 from every
    // face/layer; a volume's side 0 payload already contains its complete depth.
    asset.mipCount = 1U;
    if (image->m_cubeMap) {
        // samplerCubeArray is not part of the material graph contract. Reject it instead of silently
        // presenting a cube array as a samplerCube resource.
        if (asset.layers != 1U || asset.depth != 1U || asset.width != asset.height) {
            bimg::imageFree(image);
            return std::nullopt;
        }
        asset.dimension = RenderTextureDimension::TextureCube;
    } else if (asset.depth > 1U) {
        if (asset.layers != 1U) {
            bimg::imageFree(image);
            return std::nullopt;
        }
        asset.dimension = RenderTextureDimension::Texture3D;
    } else if (asset.layers > 1U) {
        asset.dimension = RenderTextureDimension::Texture2DArray;
    } else {
        asset.dimension = RenderTextureDimension::Texture2D;
    }

    const std::optional<std::size_t> texelCount = TextureTexelCount(asset);
    const std::size_t baseLevelBytes = texelCount.has_value() && *texelCount <= std::numeric_limits<std::size_t>::max() / 4U
        ? *texelCount * 4U
        : 0U;
    if (baseLevelBytes == 0U) {
        bimg::imageFree(image);
        return std::nullopt;
    }

    const std::uint16_t sideCount = image->m_cubeMap ? 6U : asset.layers;
    asset.rgba8.reserve(baseLevelBytes);
    for (std::uint16_t side = 0U; side < sideCount; ++side) {
        bimg::ImageMip mip{};
        if (!bimg::imageGetRawData(*image, side, 0U, image->m_data, image->m_size, mip) ||
            mip.m_data == nullptr || mip.m_format != bimg::TextureFormat::RGBA8 ||
            mip.m_width != asset.width || mip.m_height != asset.height ||
            mip.m_depth != (asset.dimension == RenderTextureDimension::Texture3D ? asset.depth : 1U) ||
            mip.m_size > baseLevelBytes - asset.rgba8.size()) {
            bimg::imageFree(image);
            return std::nullopt;
        }
        const auto* begin = static_cast<const std::uint8_t*>(mip.m_data);
        asset.rgba8.insert(asset.rgba8.end(), begin, begin + mip.m_size);
    }
    if (asset.rgba8.size() != baseLevelBytes) {
        bimg::imageFree(image);
        return std::nullopt;
    }

    bimg::imageFree(image);
    return asset;
}

[[nodiscard]] std::optional<RenderTextureAssetData> LoadImageFile(const std::filesystem::path& path) {
    std::optional<std::vector<std::uint8_t>> bytes = ReadBinaryFile(path);
    if (!bytes.has_value()) {
        return std::nullopt;
    }
    return LoadImageBytes(bytes->data(), bytes->size());
}

[[nodiscard]] std::optional<RenderTextureAssetData> LoadImportedTextureContainer(const std::filesystem::path& path) {
    kb::assets::AssetMetadata metadata{};
    metadata.physicalPath = path;
    metadata.virtualPath = path.filename();
    metadata.type = "Texture";
    metadata.importCategory = "Texture";

    kb::assets::ImportedAssetLoader importedLoader;
    kb::assets::AssetLoadResult result = importedLoader.Load(kb::assets::AssetLoadRequest{
        .metadata = metadata,
        .resolvedPath = path,
    });
    if (!result.Succeeded()) {
        return std::nullopt;
    }

    const std::shared_ptr<kb::assets::ImportedAsset> imported = std::static_pointer_cast<kb::assets::ImportedAsset>(result.asset);
    if (imported == nullptr || imported->category != kb::assets::AssetImportCategory::Texture || imported->payload.empty()) {
        return std::nullopt;
    }
    return LoadImageBytes(imported->payload.data(), imported->payload.size());
}

// ---- Decoded-texture cache ---------------------------------------------------------------------------
// Decoding an image to RGBA8 (bimg PNG/DDS decode + the mip/copy loops) is expensive: a single 2048x2048
// texture measured ~1.1s in a Debug build. Worse, the runtime GPU-handle caches are keyed per kb::scene::Scene,
// so the SAME file is decoded from scratch again by every scene that references it - the main scene, the
// material preview scene, and every thumbnail scene - and again every time the material preview scene is
// rebuilt. This process-wide cache keys the DECODED pixels by (path, last-write-time, size) so a texture is
// decoded at most once while it stays unchanged on disk. That is what turns "open the Material Editor for a
// material already used in the scene" from a multi-second re-decode into an instant reuse. Least-recently-used
// entries are dropped once the retained bytes or entry count exceed their caps. A changed file (different
// write-time or size) is treated as a miss, so on-disk edits/reimports still hot-reload correctly.
constexpr std::size_t kDecodedTextureCacheByteCap = 512U * 1024U * 1024U;
constexpr std::size_t kDecodedTextureCacheEntryCap = 512U;

struct DecodedTextureCacheSlot {
    std::string path;
    std::filesystem::file_time_type writeTime{};
    std::uintmax_t fileSize = 0U;
    std::shared_ptr<const RenderTextureAssetData> data;
    std::size_t bytes = 0U;
};

std::mutex& DecodedTextureCacheMutex() {
    static std::mutex mutex;
    return mutex;
}

// Most-recently-used at the front. Every access below must hold DecodedTextureCacheMutex().
std::list<DecodedTextureCacheSlot>& DecodedTextureCacheEntries() {
    static std::list<DecodedTextureCacheSlot> entries;
    return entries;
}

std::size_t& DecodedTextureCacheBytes() {
    static std::size_t bytes = 0U;
    return bytes;
}

std::uint64_t& DecodedTextureDecodeCounter() {
    static std::uint64_t count = 0U;
    return count;
}

[[nodiscard]] std::shared_ptr<const RenderTextureAssetData> LookupDecodedTexture(
    const std::string& path, std::filesystem::file_time_type writeTime, std::uintmax_t fileSize) {
    std::lock_guard lock{ DecodedTextureCacheMutex() };
    std::list<DecodedTextureCacheSlot>& entries = DecodedTextureCacheEntries();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->path != path) {
            continue;
        }
        if (it->writeTime != writeTime || it->fileSize != fileSize) {
            // The file changed on disk since we cached it - drop the stale decode and force a re-decode.
            DecodedTextureCacheBytes() -= it->bytes;
            entries.erase(it);
            return nullptr;
        }
        entries.splice(entries.begin(), entries, it);
        return entries.front().data;
    }
    return nullptr;
}

// What one cache entry actually costs. A baked texture holds its payload in gpuBlocks and
// leaves rgba8 empty, so charging the cache rgba8.size() would let it retain any number of
// baked textures for free.
[[nodiscard]] std::size_t RetainedTextureBytes(const RenderTextureAssetData& asset) noexcept {
    return asset.rgba8.size() + (asset.gpuBlocks.has_value() ? asset.gpuBlocks->blocks.size() : 0U);
}

void StoreDecodedTexture(
    const std::string& path,
    std::filesystem::file_time_type writeTime,
    std::uintmax_t fileSize,
    std::shared_ptr<const RenderTextureAssetData> data,
    std::size_t bytes) {
    std::lock_guard lock{ DecodedTextureCacheMutex() };
    std::list<DecodedTextureCacheSlot>& entries = DecodedTextureCacheEntries();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->path == path) {
            // A concurrent decode of the same file may have inserted first; replace instead of duplicating.
            DecodedTextureCacheBytes() -= it->bytes;
            entries.erase(it);
            break;
        }
    }
    entries.push_front(DecodedTextureCacheSlot{
        .path = path,
        .writeTime = writeTime,
        .fileSize = fileSize,
        .data = std::move(data),
        .bytes = bytes,
    });
    DecodedTextureCacheBytes() += bytes;
    while ((DecodedTextureCacheBytes() > kDecodedTextureCacheByteCap || entries.size() > kDecodedTextureCacheEntryCap) &&
        entries.size() > 1U) {
        DecodedTextureCacheBytes() -= entries.back().bytes;
        entries.pop_back();
    }
}

// The one production path from a baked package to a runtime texture, and the first producer of
// RenderTextureAssetData::gpuBlocks outside a test: everything else this loader can open is a
// source image, which it decodes to RGBA8 exactly as it always has.
//
// A pack is addressed as a whole, and the texture it carries is the ONE artifact in it whose
// type is the texture baker's. A pack holding several of them is refused rather than guessed
// at: a profile may carry more than one compression family (a browser guarantees BC or ASTC
// and only says which at runtime), the family is folded into the bake key rather than spelled
// out in the index, and picking the wrong one would hand a device blocks it has to unpack on
// the CPU - the exact cost the bake exists to remove. Choosing between families is a runtime
// selection that does not exist yet, and a silent wrong answer is worse than a refusal.
[[nodiscard]] std::optional<RenderTextureAssetData> LoadBakedTexturePack(const std::filesystem::path& path) {
    kb::assets::bake::AssetPackReader pack;
    if (pack.Mount(path) != kb::assets::bake::AssetPackReadStatus::Success) {
        return std::nullopt;
    }
    const kb::assets::bake::AssetPackArtifactEntry* texture = nullptr;
    for (const kb::assets::bake::AssetPackArtifactEntry& artifact : pack.Artifacts()) {
        if (artifact.assetTypeId != kb::render::bake::kTextureBakedAssetTypeId) {
            continue;
        }
        if (texture != nullptr) {
            return std::nullopt;
        }
        texture = &artifact;
    }
    if (texture == nullptr) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> primaryBlock;
    if (pack.ReadBlock(*texture, kb::assets::bake::kBakedAssetPrimaryBlockName, primaryBlock) !=
        kb::assets::bake::AssetPackReadStatus::Success) {
        return std::nullopt;
    }
    RenderTextureAssetData asset{};
    if (!kb::render::bake::ReadBakedTexture(primaryBlock, asset)) {
        return std::nullopt;
    }
    return asset;
}

[[nodiscard]] std::optional<RenderTextureAssetData> DecodeTextureFile(const std::filesystem::path& path) {
    if (LowerExtension(path) == kb::assets::bake::kAssetPackFileExtension) {
        return LoadBakedTexturePack(path);
    }
    if (LowerExtension(path) == ".21kb") {
        return LoadImportedTextureContainer(path);
    }
    if (LowerExtension(path) != ".kbtex") {
        return LoadImageFile(path);
    }
    std::ifstream input{ path };
    if (!input) {
        return std::nullopt;
    }
    return RenderTextureAssetLoader::LoadTexture(input);
}

// ---- Async decode worker -------------------------------------------------------------------------------
// Decoding a large image is ~1s in a Debug build, and it used to happen synchronously on the render thread the
// first time a texture was referenced (opening a material, or picking a texture in the Image Texture node) -
// that was the 1-2s freeze. The decode now runs on a single background worker that just populates the cache
// above; the render thread asks TryAcquireDecodedTexture and, on a miss, queues the decode and carries on, so
// the texture streams in a frame or two later instead of stalling. One worker keeps decodes serialized (they are
// bimg-bound, not parallelism-bound) and the cache mutex already makes the hand-off safe.
struct AsyncTextureDecodeState {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::string> queue;
    std::unordered_set<std::string> pending;
    std::thread worker;
    bool stop = false;
};

void AsyncTextureDecodeWork(AsyncTextureDecodeState* state) {
    for (;;) {
        std::string path;
        {
            std::unique_lock<std::mutex> lock{ state->mutex };
            state->wake.wait(lock, [state] { return state->stop || !state->queue.empty(); });
            if (state->stop) {
                return;
            }
            path = std::move(state->queue.front());
            state->queue.pop_front();
        }
        std::error_code writeTimeError;
        const std::filesystem::path fsPath{ path };
        const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(fsPath, writeTimeError);
        std::error_code sizeError;
        const std::uintmax_t fileSize = std::filesystem::file_size(fsPath, sizeError);
        if (!writeTimeError && !sizeError && fileSize > 0U && !LookupDecodedTexture(path, writeTime, fileSize)) {
            if (std::optional<RenderTextureAssetData> decoded = DecodeTextureFile(fsPath); decoded.has_value()) {
                {
                    std::lock_guard<std::mutex> lock{ DecodedTextureCacheMutex() };
                    ++DecodedTextureDecodeCounter();
                }
                StoreDecodedTexture(
                    path, writeTime, fileSize, std::make_shared<const RenderTextureAssetData>(*decoded),
                    RetainedTextureBytes(*decoded));
            }
        }
        {
            std::lock_guard<std::mutex> lock{ state->mutex };
            state->pending.erase(path);
        }
    }
}

AsyncTextureDecodeState& AsyncTextureDecode() {
    static AsyncTextureDecodeState* state = [] {
        // Force the decoded-texture cache singletons to exist BEFORE this atexit is registered, so at process
        // exit the handler (which stops+joins the worker) runs before those cache singletons are torn down -
        // the worker touches the cache, so it must be stopped first.
        static_cast<void>(DecodedTextureCacheMutex());
        auto* created = new AsyncTextureDecodeState(); // process-lifetime; the worker is joined by the atexit below
        created->worker = std::thread(AsyncTextureDecodeWork, created);
        static_cast<void>(std::atexit([] {
            AsyncTextureDecodeState& live = AsyncTextureDecode();
            {
                std::lock_guard<std::mutex> lock{ live.mutex };
                live.stop = true;
            }
            live.wake.notify_all();
            if (live.worker.joinable()) {
                live.worker.join();
            }
        }));
        return created;
    }();
    return *state;
}

void QueueAsyncTextureDecode(const std::string& key) {
    AsyncTextureDecodeState& state = AsyncTextureDecode();
    std::lock_guard<std::mutex> lock{ state.mutex };
    if (state.pending.insert(key).second) {
        state.queue.push_back(key);
        state.wake.notify_one();
    }
}

// Opt-in, off by default. The runtime texture ensurer only streams (async) when this is set - which the editor
// APP turns on at startup. Tests, headless self-tests and one-shot render/GPU proofs keep the deterministic
// synchronous decode (a texture is bound in the same submit), so they are not perturbed by streaming timing.
std::atomic<bool>& AsyncTextureDecodeEnabledFlag() noexcept {
    static std::atomic<bool> enabled{ false };
    return enabled;
}

} // namespace

RenderTextureDesc RenderTextureAssetData::MakeDesc(const bgfx::Memory* memory, RenderTextureColorSpace runtimeColorSpace) const noexcept {
    return RenderTextureDesc{
        .width = width,
        .height = height,
        .depth = depth,
        .layers = layers,
        .mipCount = mipCount,
        .dimension = dimension,
        .format = gpuBlocks.has_value() ? gpuBlocks->format : bgfx::TextureFormat::RGBA8,
        .flags = BGFX_SAMPLER_NONE | (runtimeColorSpace == RenderTextureColorSpace::Srgb ? BGFX_TEXTURE_SRGB : 0ULL),
        .memory = memory,
        .colorSpace = runtimeColorSpace,
    };
}

RenderTextureUploadPath SelectRenderTextureUploadPath(
    const RenderTextureAssetData& asset,
    bool deviceSupportsBakedFormat) noexcept {
    if (!asset.gpuBlocks.has_value() || asset.gpuBlocks->blocks.empty()) {
        return RenderTextureUploadPath::DecodedRgba8;
    }
    // A baked payload that somehow says RGBA8 is not a block format and has nothing to gain
    // from this path; treat it as unbaked rather than inventing a second RGBA8 upload route.
    if (asset.gpuBlocks->format == bgfx::TextureFormat::RGBA8) {
        return RenderTextureUploadPath::DecodedRgba8;
    }
    return deviceSupportsBakedFormat ? RenderTextureUploadPath::GpuBlocks : RenderTextureUploadPath::DecodedRgba8;
}

bool RenderTextureFormatCapabilitySatisfied(
    std::uint32_t formatCapabilities,
    RenderTextureColorSpace colorSpace) noexcept {
    // TEXTURE_2D means the device samples the format natively. The EMULATED bit is deliberately
    // not accepted: bgfx satisfies it by converting the surface on the CPU, which is the cost a
    // baked block format exists to avoid, and for a block format there is no conversion anyway.
    const std::uint32_t required = colorSpace == RenderTextureColorSpace::Srgb
        ? static_cast<std::uint32_t>(BGFX_CAPS_FORMAT_TEXTURE_2D | BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB)
        : static_cast<std::uint32_t>(BGFX_CAPS_FORMAT_TEXTURE_2D);
    return (formatCapabilities & required) == required;
}

bool RenderDeviceSupportsTextureFormat(bgfx::TextureFormat::Enum format, RenderTextureColorSpace colorSpace) noexcept {
    if (format < 0 || format >= bgfx::TextureFormat::Count) {
        return false;
    }
    // bgfx::getCaps() hands back a pointer to a global that exists from process start and is
    // only filled in by bgfx::init, so it is never null and it reads all-zero before a device
    // comes up. That zero is the answer we want with no device: nothing is supported, so a
    // caller decodes instead of guessing.
    return RenderTextureFormatCapabilitySatisfied(
        bgfx::getCaps()->formats[static_cast<std::size_t>(format)], colorSpace);
}

std::optional<RenderTextureAssetData> DecodeRenderTextureToRgba8(const RenderTextureAssetData& asset) {
    if (!asset.gpuBlocks.has_value()) {
        return asset;
    }
    if (asset.width == 0U || asset.height == 0U || asset.dimension != RenderTextureDimension::Texture2D ||
        asset.depth != 1U || asset.layers != 1U) {
        return std::nullopt;
    }

    const auto format = static_cast<bimg::TextureFormat::Enum>(asset.gpuBlocks->format);
    // Only a block format is decoded back. bimg's uncompressed fall-through fills the target
    // with a checkerboard for anything it does not know, and a checkerboard rendered as if it
    // were the texture is precisely the silent wrongness this fallback exists to prevent.
    if (format < 0 || format >= bimg::TextureFormat::Count || !bimg::isCompressed(format)) {
        return std::nullopt;
    }
    const std::uint32_t levelBytes = bimg::imageGetSize(
        nullptr, asset.width, asset.height, 1U, false, false, 1U, format);
    if (levelBytes == 0U || asset.gpuBlocks->blocks.size() < levelBytes) {
        return std::nullopt;
    }

    RenderTextureAssetData decoded = asset;
    decoded.gpuBlocks.reset();
    decoded.mipCount = 1U;
    decoded.rgba8.assign(static_cast<std::size_t>(asset.width) * asset.height * 4U, 0U);

    bx::DefaultAllocator allocator;
    bimg::imageDecodeToRgba8(
        &allocator,
        decoded.rgba8.data(),
        asset.gpuBlocks->blocks.data(),
        asset.width,
        asset.height,
        static_cast<std::uint32_t>(asset.width) * 4U,
        format);
    return decoded;
}

std::string_view RenderTextureAssetLoader::Type() const noexcept {
    return "RenderTexture";
}

std::type_index RenderTextureAssetLoader::PayloadType() const noexcept {
    return typeid(RenderTextureAssetData);
}

std::vector<std::string> RenderTextureAssetLoader::Extensions() const {
    return { ".kbtex", ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".dds", ".ktx",
        std::string{ kb::assets::bake::kAssetPackFileExtension } };
}

kb::assets::AssetLoadResult RenderTextureAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::optional<RenderTextureAssetData> texture = LoadTexture(request.resolvedPath);
    if (!texture.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Render texture asset load failed" };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<RenderTextureAssetData>(std::move(*texture)),
        .error = {},
    };
}

std::optional<RenderTextureAssetData> RenderTextureAssetLoader::LoadTexture(const std::filesystem::path& path) {
    // Freshness key: two cheap stats, never a decode. If either fails (missing file, virtual path) the texture
    // is treated as uncacheable and decoded straight through, so behaviour is unchanged for those.
    std::error_code writeTimeError;
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(path, writeTimeError);
    std::error_code sizeError;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, sizeError);
    const bool cacheable = !writeTimeError && !sizeError && fileSize > 0U;
    const std::string key = cacheable ? path.generic_string() : std::string{};

    if (cacheable) {
        if (const std::shared_ptr<const RenderTextureAssetData> hit = LookupDecodedTexture(key, writeTime, fileSize)) {
            return *hit;
        }
    }

    std::optional<RenderTextureAssetData> decoded = DecodeTextureFile(path);
    if (cacheable && decoded.has_value()) {
        {
            std::lock_guard lock{ DecodedTextureCacheMutex() };
            ++DecodedTextureDecodeCounter();
        }
        StoreDecodedTexture(
            key, writeTime, fileSize, std::make_shared<const RenderTextureAssetData>(*decoded),
            RetainedTextureBytes(*decoded));
    }
    return decoded;
}

std::uint64_t RenderTextureAssetLoader::DebugDecodeCount() noexcept {
    std::lock_guard lock{ DecodedTextureCacheMutex() };
    return DecodedTextureDecodeCounter();
}

std::shared_ptr<const RenderTextureAssetData> RenderTextureAssetLoader::TryAcquireDecodedTexture(const std::filesystem::path& path) {
    // Non-blocking: return the decoded texture ONLY if it is already cached (and still fresh on disk). Never
    // decodes here - that is the whole point, so the render thread never stalls.
    std::error_code writeTimeError;
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(path, writeTimeError);
    std::error_code sizeError;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, sizeError);
    if (writeTimeError || sizeError || fileSize == 0U) {
        return nullptr;
    }
    return LookupDecodedTexture(path.generic_string(), writeTime, fileSize);
}

void RenderTextureAssetLoader::RequestAsyncTextureDecode(const std::filesystem::path& path) {
    std::error_code error;
    if (path.empty() || !std::filesystem::exists(path, error)) {
        return;
    }
    QueueAsyncTextureDecode(path.generic_string());
}

void RenderTextureAssetLoader::SetAsyncTextureDecodeEnabled(bool enabled) noexcept {
    AsyncTextureDecodeEnabledFlag().store(enabled, std::memory_order_relaxed);
}

bool RenderTextureAssetLoader::IsAsyncTextureDecodeEnabled() noexcept {
    return AsyncTextureDecodeEnabledFlag().load(std::memory_order_relaxed);
}

std::optional<RenderTextureAssetData> RenderTextureAssetLoader::LoadTexture(std::istream& input) {
    RenderTextureAssetData asset{};
    std::uint8_t rgba[4]{ 255U, 255U, 255U, 255U };
    bool sawSize = false;
    bool sawColor = false;

    std::string line;
    while (std::getline(input, line)) {
        std::string_view trimmed = Trim(StripComment(line));
        if (trimmed.empty()) {
            continue;
        }

        const std::size_t keywordEnd = trimmed.find_first_of(" \t");
        const std::string_view keyword = keywordEnd == std::string_view::npos ? trimmed : trimmed.substr(0U, keywordEnd);
        const std::string_view rest = keywordEnd == std::string_view::npos ? std::string_view{} : Trim(trimmed.substr(keywordEnd + 1U));
        if (keyword == "size") {
            if (!ParseSize(rest, asset)) {
                return std::nullopt;
            }
            sawSize = true;
        } else if (keyword == "dimension") {
            if (!ParseDimension(rest, asset.dimension)) {
                return std::nullopt;
            }
        } else if (keyword == "depth") {
            if (!ParseUnsigned(rest, asset.depth) || asset.depth == 0U) {
                return std::nullopt;
            }
        } else if (keyword == "layers") {
            if (!ParseUnsigned(rest, asset.layers) || asset.layers == 0U) {
                return std::nullopt;
            }
        } else if (keyword == "colorSpace") {
            if (!ParseColorSpace(rest, asset.colorSpace)) {
                return std::nullopt;
            }
        } else if (keyword == "semantic") {
            if (!ParseSemantic(rest, asset.semantic)) {
                return std::nullopt;
            }
        } else if (keyword == "rgba8") {
            if (!ParseRgba8(rest, rgba)) {
                return std::nullopt;
            }
            sawColor = true;
        } else {
            return std::nullopt;
        }
    }

    if (!sawSize || !sawColor) {
        return std::nullopt;
    }

    return FillTexture(asset, rgba) ? std::optional<RenderTextureAssetData>{ std::move(asset) } : std::nullopt;
}

} // namespace kb::render
