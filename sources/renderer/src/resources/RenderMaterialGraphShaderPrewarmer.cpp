#include "kb/render/resources/RenderMaterialGraphShaderPrewarmer.hpp"

#include <bgfx/bgfx.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#endif

namespace kb::render {
namespace {

// Little-endian cursor over the blob with strict bounds checking; any out-of-range read fails the
// whole parse (returns false) rather than reading garbage.
struct BlobCursor {
    std::span<const std::uint8_t> data;
    std::size_t offset = 0U;

    [[nodiscard]] bool ReadU8(std::uint8_t& value) noexcept {
        if (offset + 1U > data.size()) {
            return false;
        }
        value = data[offset];
        offset += 1U;
        return true;
    }
    [[nodiscard]] bool ReadU16(std::uint16_t& value) noexcept {
        if (offset + 2U > data.size()) {
            return false;
        }
        value = static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset]) |
            (static_cast<std::uint16_t>(data[offset + 1U]) << 8U));
        offset += 2U;
        return true;
    }
    [[nodiscard]] bool ReadU32(std::uint32_t& value) noexcept {
        if (offset + 4U > data.size()) {
            return false;
        }
        value = static_cast<std::uint32_t>(data[offset]) |
            (static_cast<std::uint32_t>(data[offset + 1U]) << 8U) |
            (static_cast<std::uint32_t>(data[offset + 2U]) << 16U) |
            (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
        offset += 4U;
        return true;
    }
    [[nodiscard]] bool Skip(std::size_t count) noexcept {
        if (offset + count > data.size()) {
            return false;
        }
        offset += count;
        return true;
    }
};

[[nodiscard]] std::uint64_t HashBytes(const void* data, std::uint32_t size) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::uint32_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

std::mutex& PrewarmMutex() noexcept {
    static std::mutex mutex;
    return mutex;
}

std::unordered_set<std::uint64_t>& WarmedBytecodeHashes() {
    static std::unordered_set<std::uint64_t> hashes;
    return hashes;
}

std::uint64_t g_prewarmCount = 0U;

// DIAGNOSTIC (temporary): append one line per warmed shader so we can see, from a real editing session,
// whether the driver caches CreatePixelShader by bytecode (second create fast) or recompiles every time
// (second create as slow as the first). Best-effort; failures are swallowed.
void AppendPrewarmLog(PrewarmShaderStage stage, std::uint32_t size, double firstMs, double secondMs) noexcept {
    try {
        const std::filesystem::path path = std::filesystem::path{ "Saved" } / "Logs" / "editor-shader-prewarm.log";
        std::error_code dirError;
        std::filesystem::create_directories(path.parent_path(), dirError);
        std::ofstream out{ path, std::ios::app };
        if (!out.is_open()) {
            return;
        }
        const char* stageName = stage == PrewarmShaderStage::Fragment ? "F" : (stage == PrewarmShaderStage::Vertex ? "V" : "C");
        std::ostringstream row;
        row << "[prewarm] stage=" << stageName << " bytes=" << size
            << " firstCreate=" << firstMs << "ms secondCreate=" << secondMs << "ms\n";
        out << row.str();
    } catch (...) {
        // diagnostic only
    }
}

} // namespace

std::optional<PrewarmShaderBytecode> ExtractBgfxShaderBytecode(std::span<const std::uint8_t> blob) noexcept {
    BlobCursor cursor{ blob, 0U };

    std::uint32_t magic = 0U;
    if (!cursor.ReadU32(magic)) {
        return std::nullopt;
    }
    // bgfx magic is BX_MAKEFOURCC(type, 'S', 'H', version): byte0 = stage, bytes1..2 = "SH",
    // byte3 = blob version. Reject anything that is not a recognised shader blob.
    const std::uint8_t stageByte = static_cast<std::uint8_t>(magic & 0xFFU);
    const std::uint8_t byte1 = static_cast<std::uint8_t>((magic >> 8U) & 0xFFU);
    const std::uint8_t byte2 = static_cast<std::uint8_t>((magic >> 16U) & 0xFFU);
    const std::uint8_t version = static_cast<std::uint8_t>((magic >> 24U) & 0xFFU);
    if (byte1 != static_cast<std::uint8_t>('S') || byte2 != static_cast<std::uint8_t>('H')) {
        return std::nullopt;
    }
    PrewarmShaderStage stage{};
    switch (stageByte) {
    case static_cast<std::uint8_t>('F'): stage = PrewarmShaderStage::Fragment; break;
    case static_cast<std::uint8_t>('V'): stage = PrewarmShaderStage::Vertex; break;
    case static_cast<std::uint8_t>('C'): stage = PrewarmShaderStage::Compute; break;
    default: return std::nullopt;
    }

    std::uint32_t hashIn = 0U;
    if (!cursor.ReadU32(hashIn)) {
        return std::nullopt;
    }
    if (version >= 6U) {
        std::uint32_t hashOut = 0U;
        if (!cursor.ReadU32(hashOut)) {
            return std::nullopt;
        }
    }

    std::uint16_t uniformCount = 0U;
    if (!cursor.ReadU16(uniformCount)) {
        return std::nullopt;
    }
    for (std::uint16_t index = 0U; index < uniformCount; ++index) {
        std::uint8_t nameSize = 0U;
        if (!cursor.ReadU8(nameSize) || !cursor.Skip(nameSize)) {
            return std::nullopt;
        }
        std::uint8_t type = 0U;
        std::uint8_t num = 0U;
        std::uint16_t regIndex = 0U;
        std::uint16_t regCount = 0U;
        if (!cursor.ReadU8(type) || !cursor.ReadU8(num) || !cursor.ReadU16(regIndex) || !cursor.ReadU16(regCount)) {
            return std::nullopt;
        }
        if (version >= 8U) {
            std::uint16_t texInfo = 0U;
            if (!cursor.ReadU16(texInfo)) {
                return std::nullopt;
            }
        }
        if (version >= 10U) {
            std::uint16_t texFormat = 0U;
            if (!cursor.ReadU16(texFormat)) {
                return std::nullopt;
            }
        }
    }

    std::uint32_t shaderSize = 0U;
    if (!cursor.ReadU32(shaderSize) || shaderSize == 0U) {
        return std::nullopt;
    }
    if (cursor.offset + shaderSize > blob.size()) {
        return std::nullopt;
    }

    PrewarmShaderBytecode result{};
    result.stage = stage;
    result.code = blob.data() + cursor.offset;
    result.size = shaderSize;
    return result;
}

bool PrewarmGraphShaderBlob(std::span<const std::uint8_t> blob) noexcept {
#if defined(_WIN32)
    if (bgfx::getRendererType() != bgfx::RendererType::Direct3D11) {
        return false;
    }
    const std::optional<PrewarmShaderBytecode> bytecode = ExtractBgfxShaderBytecode(blob);
    if (!bytecode.has_value() || bytecode->stage == PrewarmShaderStage::Compute) {
        // Compute graph shaders never feed the preview mesh draw, so there is nothing to warm.
        return false;
    }

    const std::uint64_t hash = HashBytes(bytecode->code, bytecode->size);
    {
        std::lock_guard<std::mutex> lock{ PrewarmMutex() };
        if (!WarmedBytecodeHashes().insert(hash).second) {
            return false; // already warmed this process
        }
    }

    const bgfx::InternalData* internal = bgfx::getInternalData();
    auto* device = internal != nullptr ? static_cast<ID3D11Device*>(internal->context) : nullptr;
    if (device == nullptr) {
        return false;
    }

    // Creating the shader forces the driver to compile the DXBC to hardware ISA and (on drivers that
    // maintain an in-process bytecode cache) cache it. We only want that side effect, so release the
    // object immediately.
    //
    // DIAGNOSTIC (temporary): create the shader TWICE and time both. If the second create is fast, this
    // driver caches by bytecode -> pre-warming on the worker thread will make the render-thread create a
    // cache hit. If both are equally slow, this driver recompiles every CreatePixelShader and pre-warming
    // cannot help -- we then need a different strategy for the endFrame stall.
    const auto createOnce = [&](double& outMs) -> bool {
        const auto start = std::chrono::steady_clock::now();
        bool ok = false;
        if (bytecode->stage == PrewarmShaderStage::Fragment) {
            ID3D11PixelShader* shader = nullptr;
            if (SUCCEEDED(device->CreatePixelShader(bytecode->code, bytecode->size, nullptr, &shader)) && shader != nullptr) {
                shader->Release();
                ok = true;
            }
        } else {
            ID3D11VertexShader* shader = nullptr;
            if (SUCCEEDED(device->CreateVertexShader(bytecode->code, bytecode->size, nullptr, &shader)) && shader != nullptr) {
                shader->Release();
                ok = true;
            }
        }
        outMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        return ok;
    };

    double firstMs = 0.0;
    double secondMs = 0.0;
    const bool warmed = createOnce(firstMs);
    if (warmed) {
        static_cast<void>(createOnce(secondMs));
        AppendPrewarmLog(bytecode->stage, bytecode->size, firstMs, secondMs);
        std::lock_guard<std::mutex> lock{ PrewarmMutex() };
        ++g_prewarmCount;
    }
    return warmed;
#else
    static_cast<void>(blob);
    return false;
#endif
}

bool PrewarmGraphShaderFile(const std::filesystem::path& path) noexcept {
    std::error_code existsError;
    if (path.empty() || !std::filesystem::exists(path, existsError)) {
        return false;
    }
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return false;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return false;
    }
    return PrewarmGraphShaderBlob(std::span<const std::uint8_t>{ bytes.data(), bytes.size() });
}

std::uint64_t GraphShaderPrewarmCount() noexcept {
    std::lock_guard<std::mutex> lock{ PrewarmMutex() };
    return g_prewarmCount;
}

void ResetGraphShaderPrewarmStateForTesting() noexcept {
    std::lock_guard<std::mutex> lock{ PrewarmMutex() };
    WarmedBytecodeHashes().clear();
    g_prewarmCount = 0U;
}

} // namespace kb::render
