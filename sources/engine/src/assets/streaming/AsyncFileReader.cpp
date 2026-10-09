#include "engine/assets/streaming/AsyncFileReader.hpp"

#include "engine/platform/FileSystemPath.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #include <malloc.h>
#else
    #include <cerrno>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace kb::assets::streaming {
namespace {

// One request reads at most this much. Content streaming reads one pack block at a time, and a
// block is at most 128 MiB; the ceiling keeps every read inside one 32-bit ReadFile call.
constexpr std::uint64_t kMaxRequestBytes = 1024ULL * 1024ULL * 1024ULL;

[[nodiscard]] std::uint64_t AlignDown(std::uint64_t value, std::uint64_t alignment) noexcept {
    return value - value % alignment;
}

[[nodiscard]] std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment) noexcept {
    const std::uint64_t remainder = value % alignment;
    return remainder == 0U ? value : value + (alignment - remainder);
}

#if defined(_WIN32)
struct AlignedBufferDeleter {
    void operator()(std::uint8_t* buffer) const noexcept {
        _aligned_free(buffer);
    }
};
using AlignedBuffer = std::unique_ptr<std::uint8_t, AlignedBufferDeleter>;
#endif

} // namespace

class AsyncFileReader::FileHandle {
public:
    FileHandle() = default;
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    ~FileHandle() {
#if defined(_WIN32)
        if (handle != INVALID_HANDLE_VALUE) {
            static_cast<void>(CloseHandle(handle));
        }
#else
        if (descriptor >= 0) {
            static_cast<void>(::close(descriptor));
        }
#endif
    }

#if defined(_WIN32)
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int descriptor = -1;
#endif
    // Unbuffered reads must start, end and land on this boundary; 1 for a buffered handle.
    std::uint64_t alignment = 1U;
    bool unbuffered = false;
};

AsyncFileReader::AsyncFileReader(AsyncFileReaderOptions options)
    : options_{ options } {
    options_.workerCount = std::clamp<std::uint32_t>(options_.workerCount, 1U, 16U);
    options_.requestsInFlightPerWorker = std::clamp<std::uint32_t>(options_.requestsInFlightPerWorker, 1U, 32U);
#if !defined(_WIN32)
    options_.requestsInFlightPerWorker = 1U;
#endif
    workers_.reserve(options_.workerCount);
    for (std::uint32_t index = 0U; index < options_.workerCount; ++index) {
        workers_.emplace_back([this] { WorkerLoop(); });
    }
}

AsyncFileReader::~AsyncFileReader() {
    std::vector<QueueEntry> abandoned;
    {
        std::scoped_lock lock{ mutex_ };
        stopping_ = true;
        abandoned = std::move(queue_);
        queue_.clear();
    }
    wake_.notify_all();
    for (QueueEntry& entry : abandoned) {
        AsyncReadState expected = AsyncReadState::Queued;
        if (entry.request->state_.compare_exchange_strong(expected, AsyncReadState::Cancelled)) {
            entry.request->finishedAt_ = std::chrono::steady_clock::now();
        }
    }
    for (std::thread& worker : workers_) {
        worker.join();
    }
}

AsyncReadHandle AsyncFileReader::Read(
    const std::filesystem::path& path,
    std::uint64_t offset,
    std::uint64_t length,
    AsyncReadPriority priority,
    AsyncReadTransform transform) {
    auto request = std::make_shared<AsyncReadRequest>();
    request->path_ = path;
    request->offset_ = offset;
    request->length_ = length;
    request->transform_ = std::move(transform);
    return Submit(std::move(request), priority);
}

AsyncReadHandle AsyncFileReader::ReadMemory(
    std::span<const std::uint8_t> memory,
    std::uint64_t offset,
    std::uint64_t length,
    AsyncReadPriority priority,
    AsyncReadTransform transform) {
    auto request = std::make_shared<AsyncReadRequest>();
    request->memory_ = memory;
    request->fromMemory_ = true;
    request->offset_ = offset;
    request->length_ = length;
    request->transform_ = std::move(transform);
    return Submit(std::move(request), priority);
}

AsyncReadHandle AsyncFileReader::Submit(AsyncReadHandle request, AsyncReadPriority priority) {
    request->priority_.store(priority, std::memory_order_relaxed);
    request->submittedAt_ = std::chrono::steady_clock::now();
    {
        std::scoped_lock lock{ mutex_ };
        if (stopping_) {
            request->error_ = "the reader is shutting down";
            request->finishedAt_ = request->submittedAt_;
            request->state_.store(AsyncReadState::Cancelled, std::memory_order_release);
            ++stats_.cancelled;
            return request;
        }
        request->sequence_ = nextSequence_++;
        queue_.push_back(QueueEntry{ priority, request->sequence_, 0U, request });
        std::push_heap(queue_.begin(), queue_.end(), QueueOrder{});
    }
    wake_.notify_one();
    return request;
}

bool AsyncFileReader::Reprioritize(const AsyncReadHandle& request, AsyncReadPriority priority) {
    if (request == nullptr) {
        return false;
    }
    std::scoped_lock lock{ mutex_ };
    if (request->State() != AsyncReadState::Queued) {
        return false;
    }
    // The old queue entry stays behind and is skipped by its stale generation: re-heaping a
    // random element costs more than ignoring it once.
    const std::uint64_t generation = request->generation_.fetch_add(1U, std::memory_order_relaxed) + 1U;
    request->priority_.store(priority, std::memory_order_relaxed);
    queue_.push_back(QueueEntry{ priority, request->sequence_, generation, request });
    std::push_heap(queue_.begin(), queue_.end(), QueueOrder{});
    return true;
}

bool AsyncFileReader::Cancel(const AsyncReadHandle& request) {
    if (request == nullptr) {
        return false;
    }
    std::scoped_lock lock{ mutex_ };
    AsyncReadState expected = AsyncReadState::Queued;
    if (!request->state_.compare_exchange_strong(expected, AsyncReadState::Cancelled, std::memory_order_acq_rel)) {
        return false;
    }
    request->finishedAt_ = std::chrono::steady_clock::now();
    ++stats_.cancelled;
    return true;
}

void AsyncFileReader::Forget(const std::filesystem::path& path) {
    std::scoped_lock lock{ filesMutex_ };
    files_.erase(path.native());
}

AsyncFileReaderStats AsyncFileReader::Stats() const {
    std::scoped_lock lock{ mutex_ };
    return stats_;
}

std::size_t AsyncFileReader::QueuedCount() const {
    std::scoped_lock lock{ mutex_ };
    return static_cast<std::size_t>(std::ranges::count_if(queue_, [](const QueueEntry& entry) {
        return entry.request->State() == AsyncReadState::Queued &&
            entry.generation == entry.request->generation_.load(std::memory_order_relaxed);
    }));
}

bool AsyncFileReader::WaitUntilDone(const AsyncReadHandle& request, std::chrono::steady_clock::time_point deadline) {
    while (request != nullptr && !request->IsDone()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds{ 100 });
    }
    return request != nullptr;
}

std::vector<AsyncReadHandle> AsyncFileReader::TakeBatch(std::size_t count) {
    std::vector<AsyncReadHandle> batch;
    std::unique_lock lock{ mutex_ };
    for (;;) {
        while (!queue_.empty() && batch.size() < count) {
            std::pop_heap(queue_.begin(), queue_.end(), QueueOrder{});
            QueueEntry entry = std::move(queue_.back());
            queue_.pop_back();
            if (entry.generation != entry.request->generation_.load(std::memory_order_relaxed)) {
                continue;
            }
            AsyncReadState expected = AsyncReadState::Queued;
            if (entry.request->state_.compare_exchange_strong(expected, AsyncReadState::InFlight, std::memory_order_acq_rel)) {
                batch.push_back(std::move(entry.request));
            }
        }
        if (!batch.empty() || stopping_) {
            return batch;
        }
        wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
    }
}

void AsyncFileReader::WorkerLoop() {
    for (;;) {
        std::vector<AsyncReadHandle> batch = TakeBatch(options_.requestsInFlightPerWorker);
        if (batch.empty()) {
            return;
        }
        const std::uint32_t inFlight =
            inFlight_.fetch_add(static_cast<std::uint32_t>(batch.size()), std::memory_order_acq_rel) +
            static_cast<std::uint32_t>(batch.size());
        {
            std::scoped_lock lock{ mutex_ };
            stats_.peakInFlight = std::max(stats_.peakInFlight, inFlight);
        }
        ServeBatch(batch);
        inFlight_.fetch_sub(static_cast<std::uint32_t>(batch.size()), std::memory_order_acq_rel);
    }
}

std::shared_ptr<AsyncFileReader::FileHandle> AsyncFileReader::AcquireFile(
    const std::filesystem::path& path,
    std::string& error) {
    std::scoped_lock lock{ filesMutex_ };
    if (const auto found = files_.find(path.native()); found != files_.end()) {
        return found->second;
    }
    auto file = std::make_shared<FileHandle>();
#if defined(_WIN32)
    constexpr DWORD kShare = FILE_SHARE_READ | FILE_SHARE_DELETE;
    // A pack deep in a user's folders is past MAX_PATH for a raw Win32 call.
    const std::filesystem::path systemPath = kb::platform::ExtendedLengthPath(path);
    if (options_.unbuffered) {
        file->handle = CreateFileW(systemPath.c_str(), GENERIC_READ, kShare, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, nullptr);
        if (file->handle != INVALID_HANDLE_VALUE) {
            // NO_BUFFERING needs offsets, lengths and buffers on the volume's sector boundary.
            // The storage query names it; a page is the fallback, and every sector size in
            // use divides a page.
            std::uint64_t alignment = 4096U;
            FILE_STORAGE_INFO storage{};
            if (GetFileInformationByHandleEx(file->handle, FileStorageInfo, &storage, sizeof(storage)) != FALSE) {
                alignment = std::max<std::uint64_t>(
                    { 512U, storage.LogicalBytesPerSector, storage.PhysicalBytesPerSectorForPerformance });
            }
            if ((alignment & (alignment - 1U)) != 0U || alignment > 65536U) {
                alignment = 4096U;
            }
            file->alignment = alignment;
            file->unbuffered = true;
        }
    }
    if (file->handle == INVALID_HANDLE_VALUE) {
        file->handle = CreateFileW(systemPath.c_str(), GENERIC_READ, kShare, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED, nullptr);
        file->alignment = 1U;
        file->unbuffered = false;
    }
    if (file->handle == INVALID_HANDLE_VALUE) {
        error = "the file could not be opened (error " + std::to_string(GetLastError()) + ")";
        return nullptr;
    }
#else
    file->descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (file->descriptor < 0) {
        error = "the file could not be opened (errno " + std::to_string(errno) + ")";
        return nullptr;
    }
#endif
    files_.emplace(path.native(), file);
    return file;
}

void AsyncFileReader::ServeBatch(std::vector<AsyncReadHandle>& batch) {
    struct Pending {
        AsyncReadHandle request;
        std::shared_ptr<FileHandle> file;
        std::uint64_t alignedOffset = 0U;
        std::uint64_t alignedLength = 0U;
#if defined(_WIN32)
        AlignedBuffer buffer;
        OVERLAPPED overlapped{};
        bool issued = false;
#endif
    };
    std::vector<Pending> pending;
    pending.reserve(batch.size());
    for (AsyncReadHandle& request : batch) {
        if (request->length_ > kMaxRequestBytes ||
            request->offset_ > std::numeric_limits<std::uint64_t>::max() - request->length_) {
            request->error_ = "the requested range is too large";
            Finish(request, false);
            continue;
        }
        if (request->fromMemory_) {
            if (request->offset_ > request->memory_.size() || request->length_ > request->memory_.size() - request->offset_) {
                request->error_ = "the requested range lies outside the memory";
                Finish(request, false);
                continue;
            }
            request->bytes_.assign(
                request->memory_.begin() + static_cast<std::ptrdiff_t>(request->offset_),
                request->memory_.begin() + static_cast<std::ptrdiff_t>(request->offset_ + request->length_));
            {
                std::scoped_lock lock{ mutex_ };
                ++stats_.bufferedReads;
            }
            Finish(request, true);
            continue;
        }
        std::string error;
        std::shared_ptr<FileHandle> file = AcquireFile(request->path_, error);
        if (file == nullptr) {
            request->error_ = std::move(error);
            Finish(request, false);
            continue;
        }
        Pending read{};
        read.request = request;
        read.alignedOffset = AlignDown(request->offset_, file->alignment);
        read.alignedLength = AlignUp(request->offset_ + request->length_, file->alignment) - read.alignedOffset;
        read.file = std::move(file);
        pending.push_back(std::move(read));
    }

#if defined(_WIN32)
    // Issue every read of the batch before waiting for any, so the device sees them together.
    for (Pending& read : pending) {
        if (read.alignedLength == 0U) {
            continue;
        }
        const std::size_t alignment = static_cast<std::size_t>(std::max<std::uint64_t>(read.file->alignment, 64U));
        read.buffer.reset(static_cast<std::uint8_t*>(
            _aligned_malloc(static_cast<std::size_t>(read.alignedLength), alignment)));
        read.overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (read.buffer == nullptr || read.overlapped.hEvent == nullptr) {
            read.request->error_ = "no memory or event for the read";
            continue;
        }
        read.overlapped.Offset = static_cast<DWORD>(read.alignedOffset & 0xFFFFFFFFULL);
        read.overlapped.OffsetHigh = static_cast<DWORD>(read.alignedOffset >> 32U);
        if (ReadFile(read.file->handle, read.buffer.get(), static_cast<DWORD>(read.alignedLength), nullptr,
                &read.overlapped) == FALSE &&
            GetLastError() != ERROR_IO_PENDING) {
            read.request->error_ = "the read could not be issued (error " + std::to_string(GetLastError()) + ")";
            continue;
        }
        read.issued = true;
    }
    for (Pending& read : pending) {
        bool succeeded = false;
        if (read.alignedLength == 0U) {
            read.request->bytes_.clear();
            succeeded = true;
        } else if (read.issued) {
            DWORD transferred = 0U;
            if (GetOverlappedResult(read.file->handle, &read.overlapped, &transferred, TRUE) == FALSE &&
                GetLastError() != ERROR_HANDLE_EOF) {
                read.request->error_ = "the read failed (error " + std::to_string(GetLastError()) + ")";
            } else {
                // The widened range may run past the end of the file; only the requested part
                // has to be there.
                const std::uint64_t head = read.request->offset_ - read.alignedOffset;
                if (static_cast<std::uint64_t>(transferred) < head + read.request->length_) {
                    read.request->error_ = "the file ended before the requested range";
                } else {
                    read.request->bytes_.assign(read.buffer.get() + head, read.buffer.get() + head + read.request->length_);
                    succeeded = true;
                }
            }
        }
        if (read.overlapped.hEvent != nullptr) {
            static_cast<void>(CloseHandle(read.overlapped.hEvent));
        }
        read.buffer.reset();
        {
            std::scoped_lock lock{ mutex_ };
            if (read.file->unbuffered) {
                ++stats_.unbufferedReads;
            } else {
                ++stats_.bufferedReads;
            }
        }
        Finish(read.request, succeeded);
    }
#else
    for (Pending& read : pending) {
        AsyncReadRequest& request = *read.request;
        request.bytes_.assign(static_cast<std::size_t>(request.length_), 0U);
        std::uint64_t done = 0U;
        bool succeeded = true;
        while (done < request.length_) {
            const ssize_t got = ::pread(read.file->descriptor, request.bytes_.data() + done,
                static_cast<std::size_t>(request.length_ - done), static_cast<off_t>(request.offset_ + done));
            if (got < 0 && errno == EINTR) {
                continue;
            }
            if (got <= 0) {
                request.error_ = got == 0 ? "the file ended before the requested range" : "the read failed";
                succeeded = false;
                break;
            }
            done += static_cast<std::uint64_t>(got);
        }
        if (!succeeded) {
            request.bytes_.clear();
        }
        {
            std::scoped_lock lock{ mutex_ };
            ++stats_.bufferedReads;
        }
        Finish(read.request, succeeded);
    }
#endif
}

void AsyncFileReader::Finish(const AsyncReadHandle& request, bool succeeded) {
    if (succeeded && request->transform_) {
        std::string error;
        try {
            succeeded = request->transform_(request->bytes_, error);
        } catch (const std::exception& exception) {
            error = exception.what();
            succeeded = false;
        }
        if (!succeeded) {
            request->error_ = error.empty() ? "the read could not be decoded" : std::move(error);
        }
    }
    if (!succeeded) {
        request->bytes_.clear();
    }
    request->transform_ = {};
    request->finishedAt_ = std::chrono::steady_clock::now();
    {
        std::scoped_lock lock{ mutex_ };
        if (succeeded) {
            ++stats_.completed;
            stats_.bytesRead += request->length_;
        } else {
            ++stats_.failed;
        }
    }
    request->state_.store(succeeded ? AsyncReadState::Completed : AsyncReadState::Failed, std::memory_order_release);
}

} // namespace kb::assets::streaming
