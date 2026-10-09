#include "assets/bake/AssetPackCompression.hpp"

#include "engine/assets/bake/AssetPack.hpp"

#include <zstd.h>

#include <limits>
#include <memory>
#include <utility>

namespace kb::assets::bake {
namespace {

struct CompressionContextDeleter {
    void operator()(ZSTD_CCtx* context) const noexcept {
        ZSTD_freeCCtx(context);
    }
};

struct DecompressionContextDeleter {
    void operator()(ZSTD_DCtx* context) const noexcept {
        ZSTD_freeDCtx(context);
    }
};

// One decoder per thread: streaming reads decode on several I/O workers at once, and a context
// is the decoder's whole working memory, so reusing it keeps a block read allocation-light.
[[nodiscard]] ZSTD_DCtx* ThreadDecompressionContext() noexcept {
    thread_local std::unique_ptr<ZSTD_DCtx, DecompressionContextDeleter> context{ ZSTD_createDCtx() };
    return context.get();
}

} // namespace

bool CompressAssetPackBlock(std::span<const std::uint8_t> bytes, int level, std::vector<std::uint8_t>& out) {
    out.clear();
    if (bytes.empty() || bytes.size() > kMaxAssetPackBlockBytes) {
        return false;
    }
    const std::unique_ptr<ZSTD_CCtx, CompressionContextDeleter> context{ ZSTD_createCCtx() };
    if (context == nullptr) {
        return false;
    }
    // The content size goes into the frame so a reader can check it against the index before it
    // decodes; no frame checksum, because the index digest and the seal already cover the block.
    if (ZSTD_isError(ZSTD_CCtx_setParameter(context.get(), ZSTD_c_compressionLevel, level)) != 0U ||
        ZSTD_isError(ZSTD_CCtx_setParameter(context.get(), ZSTD_c_contentSizeFlag, 1)) != 0U ||
        ZSTD_isError(ZSTD_CCtx_setParameter(context.get(), ZSTD_c_checksumFlag, 0)) != 0U) {
        return false;
    }
    std::vector<std::uint8_t> compressed(ZSTD_compressBound(bytes.size()));
    const std::size_t written = ZSTD_compress2(
        context.get(), compressed.data(), compressed.size(), bytes.data(), bytes.size());
    if (ZSTD_isError(written) != 0U) {
        return false;
    }
    compressed.resize(written);
    out = std::move(compressed);
    return true;
}

bool DecompressAssetPackBlock(
    std::span<const std::uint8_t> stored,
    std::uint64_t uncompressedBytes,
    std::vector<std::uint8_t>& out) {
    out.clear();
    if (stored.empty() || uncompressedBytes == 0U || uncompressedBytes > kMaxAssetPackBlockBytes ||
        uncompressedBytes > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    // Exactly one frame, filling the stored range, that declares exactly the promised size.
    const unsigned long long declared = ZSTD_getFrameContentSize(stored.data(), stored.size());
    if (declared == ZSTD_CONTENTSIZE_UNKNOWN || declared == ZSTD_CONTENTSIZE_ERROR || declared != uncompressedBytes) {
        return false;
    }
    const std::size_t frameBytes = ZSTD_findFrameCompressedSize(stored.data(), stored.size());
    if (ZSTD_isError(frameBytes) != 0U || frameBytes != stored.size()) {
        return false;
    }
    ZSTD_DCtx* const context = ThreadDecompressionContext();
    if (context == nullptr) {
        return false;
    }
    std::vector<std::uint8_t> decoded(static_cast<std::size_t>(uncompressedBytes));
    const std::size_t produced =
        ZSTD_decompressDCtx(context, decoded.data(), decoded.size(), stored.data(), stored.size());
    if (ZSTD_isError(produced) != 0U || produced != decoded.size()) {
        // A failed frame can leave the context mid-stream; the next block must start clean.
        static_cast<void>(ZSTD_DCtx_reset(context, ZSTD_reset_session_only));
        return false;
    }
    out = std::move(decoded);
    return true;
}

} // namespace kb::assets::bake
