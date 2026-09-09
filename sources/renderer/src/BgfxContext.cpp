#include "kb/render/BgfxContext.hpp"

#include "kb/render/RenderSurface.hpp"

#include <bgfx/platform.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__ANDROID__)
#include <android/log.h>
#endif

namespace kb::render {
namespace {

[[nodiscard]] const char* BgfxFatalCodeName(bgfx::Fatal::Enum code) noexcept {
    switch (code) {
    case bgfx::Fatal::DebugCheck:
        return "DebugCheck";
    case bgfx::Fatal::InvalidShader:
        return "InvalidShader";
    case bgfx::Fatal::UnableToInitialize:
        return "UnableToInitialize";
    case bgfx::Fatal::UnableToCreateTexture:
        return "UnableToCreateTexture";
    case bgfx::Fatal::DeviceLost:
        return "DeviceLost";
    case bgfx::Fatal::Count:
        return "Count";
    }
    return "Unknown";
}

[[nodiscard]] std::filesystem::path BgfxLogPath(std::string_view writableStorageRoot) {
    if (!writableStorageRoot.empty()) {
        return std::filesystem::path{ writableStorageRoot } / "Saved" / "Logs" /
            "bgfx-fatal.log";
    }
#if defined(_WIN32)
    char tempPath[MAX_PATH]{};
    if (GetTempPathA(MAX_PATH, tempPath) != 0U) {
        return std::filesystem::path{ tempPath } / "21kb_bgfx_fatal.log";
    }
    return std::filesystem::path{ "21kb_bgfx_fatal.log" };
#else
    return {};
#endif
}

[[nodiscard]] std::filesystem::path BgfxPsoTracePath(
    std::string_view writableStorageRoot) {
    std::error_code error;
    const std::filesystem::path root = writableStorageRoot.empty()
        ? std::filesystem::current_path(error)
        : std::filesystem::path{ writableStorageRoot };
    return error || root.empty()
        ? std::filesystem::path{}
        : root / "Saved" / "Logs" / "bgfx-pso-trace.log";
}

void AppendBgfxLog(const std::filesystem::path& path, std::string_view text) {
    if (!path.empty()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (!error) {
            std::ofstream output(path, std::ios::out | std::ios::app);
            output << text;
        }
    }
#if defined(_WIN32)
    OutputDebugStringA(std::string{ text }.c_str());
#elif defined(__ANDROID__)
    __android_log_write(ANDROID_LOG_ERROR, "21kb", std::string{ text }.c_str());
#endif
}

void WriteBgfxFatalLog(
    const std::filesystem::path& logPath,
    const char* filePath,
    std::uint16_t line,
    bgfx::Fatal::Enum code,
    const char* message) {
    std::string text = "\nbgfx fatal ";
    text += BgfxFatalCodeName(code);
    text += " at ";
    text += filePath != nullptr ? filePath : "<unknown>";
    text += ":";
    text += std::to_string(line);
    text += "\n";
    text += message != nullptr ? message : "<no message>";
    text += "\n";
    AppendBgfxLog(logPath, text);
}

void AppendBgfxPsoTrace(
    const std::filesystem::path& path,
    std::uint64_t id,
    double durationMs,
    std::uint32_t cachedSize,
    bool cacheReadSucceeded,
    std::uint32_t writtenSize) {
    if (durationMs < 4.0) {
        return;
    }
    if (path.empty()) {
        return;
    }
    static std::mutex traceMutex;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return;
    }
    const auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::scoped_lock lock(traceMutex);
    std::ofstream trace{path, std::ios::out | std::ios::app};
    if (!trace.is_open()) {
        return;
    }
    trace << "epoch_ms=" << epochMs
          << " pso=0x" << std::hex << id << std::dec
          << " duration=" << durationMs << "ms"
          << " cacheHit=" << (cachedSize > 0U ? 1 : 0)
          << " cacheRead=" << (cacheReadSucceeded ? 1 : 0)
          << " cachedBytes=" << cachedSize
          << " writtenBytes=" << writtenSize << '\n';
    trace.flush();
}

[[nodiscard]] std::filesystem::path BgfxCacheRoot(
    bgfx::RendererType::Enum backend,
    std::string_view writableStorageRoot) {
    std::error_code error;
    std::filesystem::path root = writableStorageRoot.empty()
        ? std::filesystem::current_path(error)
        : std::filesystem::path{ writableStorageRoot };
    if (error) {
        error.clear();
        root = std::filesystem::temp_directory_path(error);
    }
    if (error || root.empty()) {
        return {};
    }
    return root / "Saved" / "Cache" / "bgfx" /
        std::to_string(static_cast<std::uint32_t>(backend));
}

} // namespace

class BgfxEngineCallback final : public bgfx::CallbackI {
public:
    BgfxEngineCallback(
        std::filesystem::path cacheRoot,
        std::string_view writableStorageRoot)
        : cacheRoot_(std::move(cacheRoot)),
          fatalLogPath_(BgfxLogPath(writableStorageRoot)),
          psoTracePath_(BgfxPsoTracePath(writableStorageRoot)) {
        if (!cacheRoot_.empty()) {
            std::error_code error;
            std::filesystem::create_directories(cacheRoot_, error);
        }
    }

    void fatal(const char* filePath, std::uint16_t line, bgfx::Fatal::Enum code, const char* message) override {
        WriteBgfxFatalLog(fatalLogPath_, filePath, line, code, message);
#if defined(__EMSCRIPTEN__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
        EM_ASM({
            location.hash = 'kb-error-renderer-' + encodeURIComponent(UTF8ToString($0));
        }, message != nullptr ? message : "unknown renderer failure");
#pragma clang diagnostic pop
#endif
#if defined(_WIN32)
        if (code == bgfx::Fatal::DeviceLost) {
            MessageBoxA(nullptr, message != nullptr ? message : "bgfx device lost", "21kb Engine - bgfx fatal", MB_OK | MB_ICONERROR);
        }
#else
        (void)message;
#endif
        (void)filePath;
        (void)line;
        if (code != bgfx::Fatal::DebugCheck) {
            std::abort();
        }
    }

    void traceVargs(const char*, std::uint16_t, const char*, va_list) override {}
    void profilerBegin(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerBeginLiteral(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerEnd() override {}
    std::uint32_t cacheReadSize(std::uint64_t id) override {
        std::lock_guard lock{cacheMutex_};
        if (cacheTrace_.size() >= kMaximumTrackedCacheEntries) {
            cacheTrace_.clear();
        }
        CacheTraceState& trace = cacheTrace_[id];
        trace = CacheTraceState{ .started = std::chrono::steady_clock::now() };
        const std::filesystem::path path = CachePath(id);
        if (path.empty()) {
            return 0U;
        }
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (error || size == 0U || size > kMaximumCacheEntrySize ||
            size > std::numeric_limits<std::uint32_t>::max()) {
            return 0U;
        }
        trace.cachedSize = static_cast<std::uint32_t>(size);
        return trace.cachedSize;
    }
    bool cacheRead(std::uint64_t id, void* data, std::uint32_t size) override {
        if (data == nullptr || size == 0U || size > kMaximumCacheEntrySize) {
            return false;
        }
        std::lock_guard lock{cacheMutex_};
        const std::filesystem::path path = CachePath(id);
        if (path.empty()) {
            return false;
        }
        std::ifstream input{path, std::ios::in | std::ios::binary};
        if (!input.is_open()) {
            return false;
        }
        input.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
        const bool succeeded = input.good() && input.gcount() == static_cast<std::streamsize>(size);
        if (const auto existing = cacheTrace_.find(id); existing != cacheTrace_.end()) {
            existing->second.readSucceeded = succeeded;
        }
        return succeeded;
    }
    void cacheWrite(std::uint64_t id, const void* data, std::uint32_t size) override {
        const auto completed = std::chrono::steady_clock::now();
        CacheTraceState trace{};
        bool hasTrace = false;
        {
            std::lock_guard lock{cacheMutex_};
            if (const auto existing = cacheTrace_.find(id); existing != cacheTrace_.end()) {
                trace = existing->second;
                cacheTrace_.erase(existing);
                hasTrace = true;
            }
        }
        if (hasTrace) {
            const double durationMs = std::chrono::duration<double, std::milli>(
                completed - trace.started).count();
            AppendBgfxPsoTrace(
                psoTracePath_, id, durationMs, trace.cachedSize, trace.readSucceeded, size);
        }
        if (data == nullptr || size == 0U || size > kMaximumCacheEntrySize || cacheRoot_.empty()) {
            return;
        }
        std::lock_guard lock{cacheMutex_};
        std::error_code error;
        std::filesystem::create_directories(cacheRoot_, error);
        if (error) {
            return;
        }

        const std::filesystem::path path = CachePath(id);
        std::filesystem::path temporary = path;
        temporary += ".tmp";
        {
            std::ofstream output{temporary, std::ios::out | std::ios::binary | std::ios::trunc};
            if (!output.is_open()) {
                return;
            }
            output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            output.flush();
            if (!output.good()) {
                output.close();
                std::filesystem::remove(temporary, error);
                return;
            }
        }

        error.clear();
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error) {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
        }
    }
    void screenShot(const char*, std::uint32_t, std::uint32_t, std::uint32_t, bgfx::TextureFormat::Enum, const void*, std::uint32_t, bool) override {}
    void captureBegin(std::uint32_t, std::uint32_t, std::uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, std::uint32_t) override {}

private:
    static constexpr std::uint32_t kMaximumCacheEntrySize = 64U * 1024U * 1024U;
    static constexpr std::size_t kMaximumTrackedCacheEntries = 4096U;

    struct CacheTraceState {
        std::chrono::steady_clock::time_point started{};
        std::uint32_t cachedSize = 0U;
        bool readSucceeded = false;
    };

    [[nodiscard]] std::filesystem::path CachePath(std::uint64_t id) const {
        if (cacheRoot_.empty()) {
            return {};
        }
        std::array<char, 17U> name{};
        static_cast<void>(std::snprintf(
            name.data(), name.size(), "%016llx",
            static_cast<unsigned long long>(id)));
        return cacheRoot_ / name.data();
    }

    std::filesystem::path cacheRoot_;
    std::filesystem::path fatalLogPath_;
    std::filesystem::path psoTracePath_;
    std::mutex cacheMutex_;
    std::unordered_map<std::uint64_t, CacheTraceState> cacheTrace_;
};

BgfxContext::BgfxContext() = default;

BgfxContext::~BgfxContext() {
    Shutdown();
}

bool BgfxContext::Initialize(RenderSurface& surface, const DisplayConfig& config, bgfx::RendererType::Enum preferredBackend) {
    const bool headlessNoop = config.allowHeadlessNoop && preferredBackend == bgfx::RendererType::Noop;
    if (surface.NativeWindowHandle() == nullptr && !headlessNoop) {
        return false;
    }

    return InitializeImpl(surface.Width(), surface.Height(), surface.NativeWindowHandle(), surface.NativeDisplayHandle(), config, preferredBackend);
}

bool BgfxContext::InitializeImpl(std::uint32_t width, std::uint32_t height, void* nwh, void* ndt, const DisplayConfig& config, bgfx::RendererType::Enum preferredBackend) {
    if (initialized_) {
        return true;
    }
    const bool headlessNoop = config.allowHeadlessNoop && preferredBackend == bgfx::RendererType::Noop;
    if (width == 0 || height == 0 || (nwh == nullptr && !headlessNoop)) {
        return false;
    }

    callback_ = std::make_unique<BgfxEngineCallback>(
        BgfxCacheRoot(preferredBackend, config.writableStorageRoot),
        config.writableStorageRoot);
    nativeWindowHandle_ = nwh;
    nativeDisplayHandle_ = ndt;

    bgfx::Init init{};
    resetFlags_ = config.ComputeResetFlags();
    init.callback = callback_.get();
    init.platformData.ndt = ndt;
    init.platformData.nwh = nwh;
    init.platformData.context = nullptr;
    init.platformData.backBuffer = nullptr;
    init.platformData.backBufferDS = nullptr;
    init.platformData.type = bgfx::NativeWindowHandleType::Default;
    init.resolution.width = width;
    init.resolution.height = height;
    init.resolution.reset = resetFlags_;
    init.type = preferredBackend;
    init.vendorId = BGFX_PCI_ID_NONE;

#if !defined(NDEBUG)
    init.debug = config.requestGpuDebugLayers;
#endif

    if (!bgfx::init(init)) {
        callback_.reset();
        nativeWindowHandle_ = nullptr;
        nativeDisplayHandle_ = nullptr;
        resetFlags_ = 0;
        capabilityReport_ = {};
        return false;
    }

    width_ = width;
    height_ = height;
    initialized_ = true;
    capabilityReport_ = BuildRendererCapabilityReport(preferredBackend);
#if defined(__EMSCRIPTEN__)
    bgfx::setDebug(BGFX_DEBUG_NONE);
#else
    bgfx::setDebug(BGFX_DEBUG_TEXT);
#endif
    return true;
}

void BgfxContext::Shutdown() {
    if (!initialized_) {
        return;
    }

    bgfx::shutdown();
    callback_.reset();
    initialized_ = false;
    width_ = 0;
    height_ = 0;
    resetFlags_ = 0;
    nativeWindowHandle_ = nullptr;
    nativeDisplayHandle_ = nullptr;
    capabilityReport_ = {};
}

void BgfxContext::Reset(std::uint32_t width, std::uint32_t height, std::uint32_t resetFlags) {
    if (!initialized_ || width == 0 || height == 0) {
        return;
    }

    if (width_ == width && height_ == height && resetFlags_ == resetFlags) {
        return;
    }

    width_ = width;
    height_ = height;
    resetFlags_ = resetFlags;
    bgfx::reset(width, height, resetFlags);
}

bool BgfxContext::BeginFrame() const noexcept {
    return initialized_;
}

std::uint32_t BgfxContext::EndFrame() const {
    return initialized_ ? bgfx::frame() : 0;
}

bool BgfxContext::IsInitialized() const noexcept {
    return initialized_;
}

std::uint32_t BgfxContext::Width() const noexcept {
    return width_;
}

std::uint32_t BgfxContext::Height() const noexcept {
    return height_;
}

void* BgfxContext::NativeWindowHandle() const noexcept {
    return nativeWindowHandle_;
}

const RendererCapabilityReport& BgfxContext::CapabilityReport() const noexcept {
    return capabilityReport_;
}

} // namespace kb::render
