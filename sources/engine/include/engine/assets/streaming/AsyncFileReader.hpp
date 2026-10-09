#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Asynchronous, prioritised file reads for content streaming.
//
// A dedicated pool of I/O threads serves read requests highest priority first (ties in
// submission order). On Windows every file is opened for OVERLAPPED, UNBUFFERED access
// (FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING): a request is widened to the volume's sector
// alignment, read into a sector-aligned buffer straight from the device -- the system cache is
// not polluted by gigabytes of streamed content -- and each worker keeps several such reads in
// flight at once. Elsewhere a worker reads with pread. A file that refuses unbuffered access
// (some network and virtual file systems) is read buffered and overlapped instead.
//
// A request may carry a transform that runs on the I/O thread once the bytes are in: content
// streaming verifies, decrypts and decompresses a pack block there, so nothing on the render
// thread ever waits for a disk or a decoder. The render thread only polls handles.
//
// Requests can be re-prioritised (the camera moved) or cancelled (the content is no longer
// wanted) for as long as no worker has started them.
namespace kb::assets::streaming {

// Larger is more urgent.
using AsyncReadPriority = std::int64_t;

enum class AsyncReadState : std::uint8_t {
    Queued,
    InFlight,
    Completed,
    Failed,
    Cancelled,
};

// Runs on the I/O thread after a successful read; may replace the bytes (decode them). Returning
// false fails the request with `error` as its reason.
using AsyncReadTransform = std::function<bool(std::vector<std::uint8_t>& bytes, std::string& error)>;

class AsyncFileReader;

// The shared state of one request. Owned jointly by the caller's handle and the reader.
class AsyncReadRequest {
public:
    [[nodiscard]] AsyncReadState State() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool IsDone() const noexcept {
        const AsyncReadState state = State();
        return state == AsyncReadState::Completed || state == AsyncReadState::Failed ||
            state == AsyncReadState::Cancelled;
    }
    // The bytes of a Completed request. Valid once IsDone() returned true; empty otherwise.
    [[nodiscard]] std::vector<std::uint8_t>& Bytes() noexcept {
        return bytes_;
    }
    [[nodiscard]] const std::string& Error() const noexcept {
        return error_;
    }
    // When the request was submitted and when it finished (steady clock).
    [[nodiscard]] std::chrono::steady_clock::time_point SubmittedAt() const noexcept {
        return submittedAt_;
    }
    [[nodiscard]] std::chrono::steady_clock::time_point FinishedAt() const noexcept {
        return finishedAt_;
    }
    [[nodiscard]] AsyncReadPriority Priority() const noexcept {
        return priority_.load(std::memory_order_relaxed);
    }

private:
    friend class AsyncFileReader;

    std::filesystem::path path_;
    std::span<const std::uint8_t> memory_{};
    bool fromMemory_ = false;
    std::uint64_t offset_ = 0U;
    std::uint64_t length_ = 0U;
    AsyncReadTransform transform_;
    std::atomic<AsyncReadState> state_{ AsyncReadState::Queued };
    std::atomic<AsyncReadPriority> priority_{ 0 };
    std::atomic<std::uint64_t> generation_{ 0U };
    std::uint64_t sequence_ = 0U;
    std::vector<std::uint8_t> bytes_;
    std::string error_;
    std::chrono::steady_clock::time_point submittedAt_{};
    std::chrono::steady_clock::time_point finishedAt_{};
};

using AsyncReadHandle = std::shared_ptr<AsyncReadRequest>;

struct AsyncFileReaderOptions {
    // Dedicated I/O threads.
    std::uint32_t workerCount = 2U;
    // Reads a worker keeps in flight at once (overlapped I/O). 1 on platforms without it.
    std::uint32_t requestsInFlightPerWorker = 4U;
    // Open files for unbuffered access where the platform allows it.
    bool unbuffered = true;
};

struct AsyncFileReaderStats {
    std::uint64_t completed = 0U;
    std::uint64_t failed = 0U;
    std::uint64_t cancelled = 0U;
    std::uint64_t bytesRead = 0U;
    // Reads served through the unbuffered path (Windows), and through any other path.
    std::uint64_t unbufferedReads = 0U;
    std::uint64_t bufferedReads = 0U;
    // Most reads observed in flight at the same time across all workers.
    std::uint32_t peakInFlight = 0U;
};

class AsyncFileReader {
public:
    explicit AsyncFileReader(AsyncFileReaderOptions options = {});
    AsyncFileReader(const AsyncFileReader&) = delete;
    AsyncFileReader& operator=(const AsyncFileReader&) = delete;
    // Cancels everything still queued and waits for the reads in flight.
    ~AsyncFileReader();

    // Reads `length` bytes at `offset` of the file. The file is opened once, by the first request
    // that needs it, and kept open until Forget or destruction.
    [[nodiscard]] AsyncReadHandle Read(
        const std::filesystem::path& path,
        std::uint64_t offset,
        std::uint64_t length,
        AsyncReadPriority priority,
        AsyncReadTransform transform = {});
    // Copies the range out of memory the caller keeps alive until the request is done (a pack
    // mounted from memory). Goes through the same queue, priority and transform.
    [[nodiscard]] AsyncReadHandle ReadMemory(
        std::span<const std::uint8_t> memory,
        std::uint64_t offset,
        std::uint64_t length,
        AsyncReadPriority priority,
        AsyncReadTransform transform = {});

    // Changes the priority of a request that has not started. False once a worker took it.
    bool Reprioritize(const AsyncReadHandle& request, AsyncReadPriority priority);
    // Cancels a request that has not started. False once a worker took it.
    bool Cancel(const AsyncReadHandle& request);

    // Closes the cached handle of a file once no request needs it any more.
    void Forget(const std::filesystem::path& path);

    [[nodiscard]] AsyncFileReaderStats Stats() const;
    [[nodiscard]] std::size_t QueuedCount() const;
    [[nodiscard]] std::uint32_t WorkerCount() const noexcept {
        return static_cast<std::uint32_t>(workers_.size());
    }

    // Blocks the calling thread until `request` is done or the deadline passes. For tools and
    // tests; the render thread polls instead.
    static bool WaitUntilDone(const AsyncReadHandle& request, std::chrono::steady_clock::time_point deadline);

private:
    struct QueueEntry {
        AsyncReadPriority priority = 0;
        std::uint64_t sequence = 0U;
        std::uint64_t generation = 0U;
        AsyncReadHandle request;
    };
    struct QueueOrder {
        [[nodiscard]] bool operator()(const QueueEntry& lhs, const QueueEntry& rhs) const noexcept {
            // std::push_heap keeps the largest on top: highest priority, then the oldest.
            if (lhs.priority != rhs.priority) {
                return lhs.priority < rhs.priority;
            }
            return lhs.sequence > rhs.sequence;
        }
    };
    class FileHandle;

    [[nodiscard]] AsyncReadHandle Submit(AsyncReadHandle request, AsyncReadPriority priority);
    void WorkerLoop();
    // Takes up to `count` startable requests, best first. Empty when stopping.
    [[nodiscard]] std::vector<AsyncReadHandle> TakeBatch(std::size_t count);
    [[nodiscard]] std::shared_ptr<FileHandle> AcquireFile(const std::filesystem::path& path, std::string& error);
    void ServeBatch(std::vector<AsyncReadHandle>& batch);
    void Finish(const AsyncReadHandle& request, bool succeeded);

    AsyncFileReaderOptions options_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<QueueEntry> queue_;
    std::uint64_t nextSequence_ = 0U;
    bool stopping_ = false;
    std::unordered_map<std::filesystem::path::string_type, std::shared_ptr<FileHandle>> files_;
    std::mutex filesMutex_;
    std::vector<std::thread> workers_;
    std::atomic<std::uint32_t> inFlight_{ 0U };
    AsyncFileReaderStats stats_{};
};

} // namespace kb::assets::streaming
