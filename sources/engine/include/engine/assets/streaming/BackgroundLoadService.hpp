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

// The engine's one background work service.
//
// One set of threads in the process serves all background work: content streaming reads
// (texture mips, mesh levels), the asset manager's asynchronous loads, texture decodes, scene
// preparation, and the long tool work of the editor and the renderer (asset imports, material
// cooks, packaging, thumbnails, screenshot encoding). Each user holds the instance
// BackgroundLoadService::Shared() hands out; the threads start with the first user and are
// joined when the last one lets go. Frame compute stays on the ECS worker pool; nothing here
// blocks it.
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
// - Jobs, in two classes:
//   - Load jobs load or decode something a frame is waiting for (an asset loader, an image
//     decoder). They run on the load workers, which also serve the reads: at most `jobWorkers`
//     of them run jobs at once and the rest always stay free for reads, so a slow loader never
//     starves streaming. A free load worker takes a runnable job before reads while under that
//     limit, so a steady stream of reads never starves the jobs either.
//   - Long jobs run for seconds to minutes or wait for another process (imports, cooks,
//     packaging, encoding). They run only on the long workers, which never take reads or load
//     jobs, so a long job can never occupy the capacity loads and reads rely on. Long workers
//     start when long work first needs one, up to `longJobWorkers`.
//   Jobs submitted to a lane run at most `concurrency` at a time (one by default) in priority
//   order; the lane's owner can cancel everything it queued and wait for the jobs it has
//   running.
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

// Jobs of one lane run up to the lane's concurrency at a time. 0 is no lane: such jobs only
// share the limits of their class.
using BackgroundLaneId = std::uint64_t;
inline constexpr BackgroundLaneId kNoBackgroundLane = 0U;

// Which workers run a job: the load workers (bounded work a frame waits for) or the long
// workers (tool work that may run for minutes).
enum class BackgroundJobClass : std::uint8_t {
    Load,
    Long,
};

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
    BackgroundJobClass jobClass_ = BackgroundJobClass::Load;
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
    // Load workers: reads and load jobs.
    std::uint32_t workerCount = 3U;
    // Reads a worker keeps in flight at once (overlapped I/O). 1 on platforms without it.
    std::uint32_t requestsInFlightPerWorker = 4U;
    // Workers that may run jobs at the same time. Clamped to workerCount - 1 (at least 1), so
    // with more than one worker reads always keep a worker of their own.
    std::uint32_t jobWorkers = 2U;
    // Open files for unbuffered access where the platform allows it.
    bool unbuffered = true;
    // Most long workers, started as long jobs need them. Each user of long work runs its jobs
    // in a lane, so this bounds how many such users make progress at the same time.
    std::uint32_t longJobWorkers = 8U;
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
    // Load jobs that ran to an end (completed or failed), and the most that ran at the same time.
    std::uint64_t jobsRun = 0U;
    std::uint32_t peakJobsRunning = 0U;
    // The same for long jobs.
    std::uint64_t longJobsRun = 0U;
    std::uint32_t peakLongJobsRunning = 0U;
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
    // Runs `job` in the lane, on a worker of the lane's class (a load worker for no lane). A job
    // of a lane waits while the lane runs as many jobs as its concurrency allows.
    [[nodiscard]] BackgroundRequestHandle Run(
        BackgroundJob job,
        BackgroundPriority priority,
        BackgroundLaneId lane = kNoBackgroundLane);
    // Runs `job` in no lane, on a worker of `jobClass`.
    [[nodiscard]] BackgroundRequestHandle Run(
        BackgroundJob job,
        BackgroundPriority priority,
        BackgroundJobClass jobClass);

    // Changes the priority of a request that has not started. False once a worker took it.
    bool Reprioritize(const BackgroundRequestHandle& request, BackgroundPriority priority);
    // Cancels a request that has not started. False once a worker took it.
    bool Cancel(const BackgroundRequestHandle& request);

    // A new lane, and its end: CloseLane cancels its queued jobs and waits for the running ones.
    [[nodiscard]] BackgroundLaneId OpenLane(
        BackgroundJobClass jobClass = BackgroundJobClass::Load,
        std::uint32_t concurrency = 1U);
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
    // Load workers.
    [[nodiscard]] std::uint32_t WorkerCount() const noexcept {
        return static_cast<std::uint32_t>(workers_.size());
    }
    [[nodiscard]] std::uint32_t JobWorkerLimit() const noexcept {
        return options_.jobWorkers;
    }
    // Long workers started so far, and the most there may be.
    [[nodiscard]] std::uint32_t LongWorkerCount() const;
    [[nodiscard]] std::uint32_t LongWorkerLimit() const noexcept {
        return options_.longJobWorkers;
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
        BackgroundJobClass jobClass = BackgroundJobClass::Load;
        std::uint32_t concurrency = 1U;
        std::uint32_t running = 0U;
        // Jobs taken off the queue while the lane was full; back in the queue when it frees.
        std::vector<QueueEntry> parked;
    };
    class FileHandle;

    [[nodiscard]] BackgroundRequestHandle Submit(BackgroundRequestHandle request, BackgroundPriority priority);
    void WorkerLoop();
    void LongWorkerLoop();
    // Takes one job, or up to `count` startable reads, best first. Empty when stopping.
    [[nodiscard]] std::vector<BackgroundRequestHandle> TakeBatch(std::size_t count);
    // Pops the best startable job of `queue`, parking jobs whose lane is full; null when none.
    // Needs mutex_.
    [[nodiscard]] BackgroundRequestHandle TakeJob(std::vector<QueueEntry>& queue);
    [[nodiscard]] std::vector<QueueEntry>& JobQueue(BackgroundJobClass jobClass) noexcept {
        return jobClass == BackgroundJobClass::Long ? longJobs_ : jobs_;
    }
    [[nodiscard]] static bool IsLive(const QueueEntry& entry) noexcept;
    [[nodiscard]] std::shared_ptr<FileHandle> AcquireFile(const std::filesystem::path& path, std::string& error);
    void ServeBatch(std::vector<BackgroundRequestHandle>& batch);
    void RunJob(const BackgroundRequestHandle& request);
    void Finish(const BackgroundRequestHandle& request, bool succeeded);

    BackgroundLoadServiceOptions options_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable longWake_;
    std::condition_variable laneIdle_;
    std::vector<QueueEntry> reads_;
    std::vector<QueueEntry> jobs_;
    std::vector<QueueEntry> longJobs_;
    std::unordered_map<BackgroundLaneId, Lane> lanes_;
    BackgroundLaneId nextLane_ = 1U;
    std::uint32_t jobsRunning_ = 0U;
    std::uint32_t longJobsRunning_ = 0U;
    std::uint32_t longWorkersIdle_ = 0U;
    std::uint64_t nextSequence_ = 0U;
    bool stopping_ = false;
    std::unordered_map<std::filesystem::path::string_type, std::shared_ptr<FileHandle>> files_;
    std::mutex filesMutex_;
    std::vector<std::thread> workers_;
    // Started under mutex_ as long work needs them; joined with the load workers.
    std::vector<std::thread> longWorkers_;
    std::atomic<std::uint32_t> inFlight_{ 0U };
    BackgroundLoadServiceStats stats_{};
};

// Owns one lane of a service: its jobs run up to `concurrency` at a time on the workers of
// `jobClass`, and destroying the lane cancels what it queued and waits for the jobs it has
// running. Holds the service alive.
class BackgroundLane {
public:
    explicit BackgroundLane(
        std::shared_ptr<BackgroundLoadService> service,
        BackgroundJobClass jobClass = BackgroundJobClass::Load,
        std::uint32_t concurrency = 1U);
    BackgroundLane(const BackgroundLane&) = delete;
    BackgroundLane& operator=(const BackgroundLane&) = delete;
    ~BackgroundLane();

    [[nodiscard]] BackgroundRequestHandle Run(BackgroundJob job, BackgroundPriority priority = 0);
    // Cancels every job not yet started and waits for the running ones; the lane stays usable.
    void CancelAndWait() noexcept;

    [[nodiscard]] BackgroundLoadService& Service() const noexcept {
        return *service_;
    }

private:
    std::shared_ptr<BackgroundLoadService> service_;
    BackgroundLaneId id_ = kNoBackgroundLane;
};

} // namespace kb::assets::streaming
