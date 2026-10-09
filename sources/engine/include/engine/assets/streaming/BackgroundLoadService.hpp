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

// The engine's one background loading service.
//
// One pool of threads in the process serves every background load: content streaming reads
// (texture mips, mesh levels), the asset manager's asynchronous loads and texture decodes. Each
// user holds the instance BackgroundLoadService::Shared() hands out; the threads start with the
// first user and are joined when the last one lets go. Frame compute stays on the ECS worker
// pool; nothing here blocks it.
//
// The service runs two kinds of request:
//
// - Reads. Served highest priority first (ties in submission order). On Windows every file is
//   opened for OVERLAPPED, UNBUFFERED access (FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING): a
//   request is widened to the volume's sector alignment, read into a sector-aligned buffer
//   straight from the device -- the system cache is not polluted by gigabytes of streamed
//   content -- and each worker keeps several such reads in flight at once. Elsewhere a worker
//   reads with pread. A file that refuses unbuffered access (some network and virtual file
//   systems) is read buffered and overlapped instead. A read may carry a transform that runs on
//   the worker once the bytes are in: content streaming verifies, decrypts and decompresses a
//   pack block there, so nothing on the render thread ever waits for a disk or a decoder.
//
// - Jobs. A callable that loads or decodes something (an asset loader, an image decoder). A job
//   may take long or block on its own I/O, so at most `jobWorkers` workers run jobs at once and
//   the rest always stay free for reads: a slow loader never starves streaming. A free worker
//   takes a runnable job before reads while under that limit, so a steady stream of reads never
//   starves the jobs either. Jobs submitted to a lane run one at a time in priority order (the
//   asset manager's loaders and the texture decoder are serial); the lane's owner can cancel
//   everything it queued and wait for the one job it has running.
//
// Every request can be re-prioritised or cancelled for as long as no worker has started it.
namespace kb::assets::streaming {

// Larger is more urgent.
using BackgroundPriority = std::int64_t;

enum class BackgroundRequestState : std::uint8_t {
    Queued,
    InFlight,
    Completed,
    Failed,
    Cancelled,
};

// Runs on the worker after a successful read; may replace the bytes (decode them). Returning
// false fails the request with `error` as its reason.
using BackgroundReadTransform = std::function<bool(std::vector<std::uint8_t>& bytes, std::string& error)>;

// The work of a job. Returning false (or throwing) fails the request with `error` as its reason.
using BackgroundJob = std::function<bool(std::string& error)>;

// Jobs of one lane run one at a time. 0 is no lane: such jobs only share the job limit.
using BackgroundLaneId = std::uint64_t;
inline constexpr BackgroundLaneId kNoBackgroundLane = 0U;

class BackgroundLoadService;

// The shared state of one request. Owned jointly by the caller's handle and the service.
class BackgroundRequest {
public:
    [[nodiscard]] BackgroundRequestState State() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool IsDone() const noexcept {
        const BackgroundRequestState state = State();
        return state == BackgroundRequestState::Completed || state == BackgroundRequestState::Failed ||
            state == BackgroundRequestState::Cancelled;
    }
    // The bytes of a Completed read. Valid once IsDone() returned true; empty otherwise and for jobs.
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
    [[nodiscard]] BackgroundPriority Priority() const noexcept {
        return priority_.load(std::memory_order_relaxed);
    }

private:
    friend class BackgroundLoadService;

    std::filesystem::path path_;
    std::span<const std::uint8_t> memory_{};
    bool fromMemory_ = false;
    std::uint64_t offset_ = 0U;
    std::uint64_t length_ = 0U;
    BackgroundReadTransform transform_;
    BackgroundJob job_;
    bool isJob_ = false;
    BackgroundLaneId lane_ = kNoBackgroundLane;
    std::atomic<BackgroundRequestState> state_{ BackgroundRequestState::Queued };
    std::atomic<BackgroundPriority> priority_{ 0 };
    std::atomic<std::uint64_t> generation_{ 0U };
    std::uint64_t sequence_ = 0U;
    std::vector<std::uint8_t> bytes_;
    std::string error_;
    std::chrono::steady_clock::time_point submittedAt_{};
    std::chrono::steady_clock::time_point finishedAt_{};
};

using BackgroundRequestHandle = std::shared_ptr<BackgroundRequest>;

struct BackgroundLoadServiceOptions {
    // Threads of the pool.
    std::uint32_t workerCount = 3U;
    // Reads a worker keeps in flight at once (overlapped I/O). 1 on platforms without it.
    std::uint32_t requestsInFlightPerWorker = 4U;
    // Workers that may run jobs at the same time. Clamped to workerCount - 1 (at least 1), so
    // with more than one worker reads always keep a worker of their own.
    std::uint32_t jobWorkers = 2U;
    // Open files for unbuffered access where the platform allows it.
    bool unbuffered = true;
};

struct BackgroundLoadServiceStats {
    std::uint64_t completed = 0U;
    std::uint64_t failed = 0U;
    std::uint64_t cancelled = 0U;
    std::uint64_t bytesRead = 0U;
    // Reads served through the unbuffered path (Windows), and through any other path.
    std::uint64_t unbufferedReads = 0U;
    std::uint64_t bufferedReads = 0U;
    // Most reads observed in flight at the same time across all workers.
    std::uint32_t peakInFlight = 0U;
    // Jobs that ran to an end (completed or failed), and the most that ran at the same time.
    std::uint64_t jobsRun = 0U;
    std::uint32_t peakJobsRunning = 0U;
};

class BackgroundLoadService {
public:
    explicit BackgroundLoadService(BackgroundLoadServiceOptions options = {});
    BackgroundLoadService(const BackgroundLoadService&) = delete;
    BackgroundLoadService& operator=(const BackgroundLoadService&) = delete;
    // Cancels everything still queued and waits for the requests in flight.
    ~BackgroundLoadService();

    // The process's service, created with default options when no user holds one. Every
    // engine and renderer user shares it; the last handle to go joins the threads.
    [[nodiscard]] static std::shared_ptr<BackgroundLoadService> Shared();

    // Reads `length` bytes at `offset` of the file. The file is opened once, by the first request
    // that needs it, and kept open until Forget or destruction.
    [[nodiscard]] BackgroundRequestHandle Read(
        const std::filesystem::path& path,
        std::uint64_t offset,
        std::uint64_t length,
        BackgroundPriority priority,
        BackgroundReadTransform transform = {});
    // Copies the range out of memory the caller keeps alive until the request is done (a pack
    // mounted from memory). Goes through the same queue, priority and transform.
    [[nodiscard]] BackgroundRequestHandle ReadMemory(
        std::span<const std::uint8_t> memory,
        std::uint64_t offset,
        std::uint64_t length,
        BackgroundPriority priority,
        BackgroundReadTransform transform = {});
    // Runs `job` on a worker. A job of a lane waits while another job of that lane runs.
    [[nodiscard]] BackgroundRequestHandle Run(
        BackgroundJob job,
        BackgroundPriority priority,
        BackgroundLaneId lane = kNoBackgroundLane);

    // Changes the priority of a request that has not started. False once a worker took it.
    bool Reprioritize(const BackgroundRequestHandle& request, BackgroundPriority priority);
    // Cancels a request that has not started. False once a worker took it.
    bool Cancel(const BackgroundRequestHandle& request);

    // A new lane, and its end: CloseLane cancels its queued jobs and waits for the running one.
    [[nodiscard]] BackgroundLaneId OpenLane();
    void CloseLane(BackgroundLaneId lane) noexcept;
    // Cancels every job of the lane that has not started; returns how many.
    std::size_t CancelLane(BackgroundLaneId lane) noexcept;
    // Blocks until no job of the lane is running. Must not be called from a job of that lane.
    void WaitForLane(BackgroundLaneId lane) noexcept;

    // Closes the cached handle of a file once no request needs it any more.
    void Forget(const std::filesystem::path& path);

    [[nodiscard]] BackgroundLoadServiceStats Stats() const;
    // Requests (reads and jobs) waiting for a worker.
    [[nodiscard]] std::size_t QueuedCount() const;
    [[nodiscard]] std::uint32_t WorkerCount() const noexcept {
        return static_cast<std::uint32_t>(workers_.size());
    }
    [[nodiscard]] std::uint32_t JobWorkerLimit() const noexcept {
        return options_.jobWorkers;
    }

    // Blocks the calling thread until `request` is done or the deadline passes. For tools and
    // tests; the render thread polls instead.
    static bool WaitUntilDone(const BackgroundRequestHandle& request, std::chrono::steady_clock::time_point deadline);

private:
    struct QueueEntry {
        BackgroundPriority priority = 0;
        std::uint64_t sequence = 0U;
        std::uint64_t generation = 0U;
        BackgroundRequestHandle request;
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
    struct Lane {
        bool running = false;
        // Jobs taken off the queue while the lane was busy; back in the queue when it frees.
        std::vector<QueueEntry> parked;
    };
    class FileHandle;

    [[nodiscard]] BackgroundRequestHandle Submit(BackgroundRequestHandle request, BackgroundPriority priority);
    void WorkerLoop();
    // Takes one job, or up to `count` startable reads, best first. Empty when stopping.
    [[nodiscard]] std::vector<BackgroundRequestHandle> TakeBatch(std::size_t count);
    [[nodiscard]] static bool IsLive(const QueueEntry& entry) noexcept;
    [[nodiscard]] std::shared_ptr<FileHandle> AcquireFile(const std::filesystem::path& path, std::string& error);
    void ServeBatch(std::vector<BackgroundRequestHandle>& batch);
    void RunJob(const BackgroundRequestHandle& request);
    void Finish(const BackgroundRequestHandle& request, bool succeeded);

    BackgroundLoadServiceOptions options_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable laneIdle_;
    std::vector<QueueEntry> reads_;
    std::vector<QueueEntry> jobs_;
    std::unordered_map<BackgroundLaneId, Lane> lanes_;
    BackgroundLaneId nextLane_ = 1U;
    std::uint32_t jobsRunning_ = 0U;
    std::uint64_t nextSequence_ = 0U;
    bool stopping_ = false;
    std::unordered_map<std::filesystem::path::string_type, std::shared_ptr<FileHandle>> files_;
    std::mutex filesMutex_;
    std::vector<std::thread> workers_;
    std::atomic<std::uint32_t> inFlight_{ 0U };
    BackgroundLoadServiceStats stats_{};
};

// Owns one lane of a service: jobs run one at a time, and destroying the lane cancels what it
// queued and waits for the job it has running. Holds the service alive.
class BackgroundLane {
public:
    explicit BackgroundLane(std::shared_ptr<BackgroundLoadService> service);
    BackgroundLane(const BackgroundLane&) = delete;
    BackgroundLane& operator=(const BackgroundLane&) = delete;
    ~BackgroundLane();

    [[nodiscard]] BackgroundRequestHandle Run(BackgroundJob job, BackgroundPriority priority = 0);
    // Cancels every job not yet started and waits for the running one; the lane stays usable.
    void CancelAndWait() noexcept;

    [[nodiscard]] BackgroundLoadService& Service() const noexcept {
        return *service_;
    }

private:
    std::shared_ptr<BackgroundLoadService> service_;
    BackgroundLaneId id_ = kNoBackgroundLane;
};

} // namespace kb::assets::streaming
