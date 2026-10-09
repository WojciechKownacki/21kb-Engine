#include "engine/assets/streaming/BackgroundLoadService.hpp"

#include "engine/platform/FileSystemPath.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
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

class BackgroundLoadService::FileHandle {
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

BackgroundLoadService::BackgroundLoadService(BackgroundLoadServiceOptions options)
    : options_{ options } {
    options_.workerCount = std::clamp<std::uint32_t>(options_.workerCount, 1U, 16U);
    options_.requestsInFlightPerWorker = std::clamp<std::uint32_t>(options_.requestsInFlightPerWorker, 1U, 32U);
    options_.jobWorkers = std::clamp<std::uint32_t>(
        options_.jobWorkers, 1U, std::max<std::uint32_t>(1U, options_.workerCount - 1U));
    options_.longJobWorkers = std::clamp<std::uint32_t>(options_.longJobWorkers, 1U, 64U);
#if !defined(_WIN32)
    options_.requestsInFlightPerWorker = 1U;
#endif
    workers_.reserve(options_.workerCount);
    for (std::uint32_t index = 0U; index < options_.workerCount; ++index) {
        workers_.emplace_back([this] { WorkerLoop(); });
    }
}

BackgroundLoadService::~BackgroundLoadService() {
    std::vector<QueueEntry> abandoned;
    {
        std::scoped_lock lock{ mutex_ };
        stopping_ = true;
        abandoned = std::move(reads_);
        reads_.clear();
        abandoned.insert(abandoned.end(), std::make_move_iterator(jobs_.begin()), std::make_move_iterator(jobs_.end()));
        jobs_.clear();
        abandoned.insert(abandoned.end(), std::make_move_iterator(longJobs_.begin()),
            std::make_move_iterator(longJobs_.end()));
        longJobs_.clear();
        for (auto& [id, lane] : lanes_) {
            static_cast<void>(id);
            abandoned.insert(abandoned.end(), std::make_move_iterator(lane.parked.begin()),
                std::make_move_iterator(lane.parked.end()));
            lane.parked.clear();
        }
    }
    wake_.notify_all();
    longWake_.notify_all();
    for (QueueEntry& entry : abandoned) {
        BackgroundRequestState expected = BackgroundRequestState::Queued;
        if (entry.request->state_.compare_exchange_strong(expected, BackgroundRequestState::Cancelled)) {
            entry.request->job_ = {};
            entry.request->transform_ = {};
            entry.request->finishedAt_ = std::chrono::steady_clock::now();
        }
    }
    for (std::thread& worker : workers_) {
        worker.join();
    }
    // stopping_ is set, so no long worker starts any more.
    for (std::thread& worker : longWorkers_) {
        worker.join();
    }
}

std::shared_ptr<BackgroundLoadService> BackgroundLoadService::Shared() {
    // Never destroyed: a user releasing its handle during static destruction must still find
    // the mutex. The service itself goes with its last handle.
    struct Instance {
        std::mutex mutex;
        std::weak_ptr<BackgroundLoadService> service;
    };
    static Instance* const instance = new Instance{};
    std::scoped_lock lock{ instance->mutex };
    std::shared_ptr<BackgroundLoadService> service = instance->service.lock();
    if (service == nullptr) {
        service = std::make_shared<BackgroundLoadService>();
        instance->service = service;
    }
    return service;
}

BackgroundRequestHandle BackgroundLoadService::Read(
    const std::filesystem::path& path,
    std::uint64_t offset,
    std::uint64_t length,
    BackgroundPriority priority,
    BackgroundReadTransform transform) {
    auto request = std::make_shared<BackgroundRequest>();
    request->path_ = path;
    request->offset_ = offset;
    request->length_ = length;
    request->transform_ = std::move(transform);
    return Submit(std::move(request), priority);
}

BackgroundRequestHandle BackgroundLoadService::ReadMemory(
    std::span<const std::uint8_t> memory,
    std::uint64_t offset,
    std::uint64_t length,
    BackgroundPriority priority,
    BackgroundReadTransform transform) {
    auto request = std::make_shared<BackgroundRequest>();
    request->memory_ = memory;
    request->fromMemory_ = true;
    request->offset_ = offset;
    request->length_ = length;
    request->transform_ = std::move(transform);
    return Submit(std::move(request), priority);
}

BackgroundRequestHandle BackgroundLoadService::Run(BackgroundJob job, BackgroundPriority priority, BackgroundLaneId lane) {
    auto request = std::make_shared<BackgroundRequest>();
    request->job_ = std::move(job);
    request->isJob_ = true;
    request->lane_ = lane;
    return Submit(std::move(request), priority);
}

BackgroundRequestHandle BackgroundLoadService::Run(BackgroundJob job, BackgroundPriority priority, BackgroundJobClass jobClass) {
    auto request = std::make_shared<BackgroundRequest>();
    request->job_ = std::move(job);
    request->isJob_ = true;
    request->jobClass_ = jobClass;
    return Submit(std::move(request), priority);
}

BackgroundRequestHandle BackgroundLoadService::Submit(BackgroundRequestHandle request, BackgroundPriority priority) {
    request->priority_.store(priority, std::memory_order_relaxed);
    request->submittedAt_ = std::chrono::steady_clock::now();
    {
        std::scoped_lock lock{ mutex_ };
        const auto lane = request->lane_ == kNoBackgroundLane ? lanes_.end() : lanes_.find(request->lane_);
        const bool laneClosed = request->lane_ != kNoBackgroundLane && lane == lanes_.end();
        if (stopping_ || laneClosed) {
            request->error_ = stopping_ ? "the background load service is shutting down" : "the lane is closed";
            request->job_ = {};
            request->transform_ = {};
            request->finishedAt_ = request->submittedAt_;
            request->state_.store(BackgroundRequestState::Cancelled, std::memory_order_release);
            ++stats_.cancelled;
            return request;
        }
        if (lane != lanes_.end()) {
            request->jobClass_ = lane->second.jobClass;
        }
        const bool longJob = request->isJob_ && request->jobClass_ == BackgroundJobClass::Long;
        if (longJob) {
            // One long worker for every long job that could run now, up to the limit. A thread
            // that cannot start throws before anything is queued.
            const std::size_t demand = static_cast<std::size_t>(longJobsRunning_) + 1U +
                static_cast<std::size_t>(std::ranges::count_if(longJobs_, IsLive));
            if (longWorkers_.size() < std::min<std::size_t>(demand, options_.longJobWorkers)) {
                longWorkers_.emplace_back([this] { LongWorkerLoop(); });
            }
        }
        request->sequence_ = nextSequence_++;
        std::vector<QueueEntry>& queue = request->isJob_ ? JobQueue(request->jobClass_) : reads_;
        queue.push_back(QueueEntry{ priority, request->sequence_, 0U, request });
        std::push_heap(queue.begin(), queue.end(), QueueOrder{});
        if (longJob) {
            longWake_.notify_all();
            return request;
        }
    }
    wake_.notify_one();
    return request;
}

bool BackgroundLoadService::Reprioritize(const BackgroundRequestHandle& request, BackgroundPriority priority) {
    if (request == nullptr) {
        return false;
    }
    {
        std::scoped_lock lock{ mutex_ };
        if (request->State() != BackgroundRequestState::Queued) {
            return false;
        }
        // The old queue entry stays behind and is skipped by its stale generation: re-heaping a
        // random element costs more than ignoring it once.
        const std::uint64_t generation = request->generation_.fetch_add(1U, std::memory_order_relaxed) + 1U;
        request->priority_.store(priority, std::memory_order_relaxed);
        std::vector<QueueEntry>& queue = request->isJob_ ? JobQueue(request->jobClass_) : reads_;
        queue.push_back(QueueEntry{ priority, request->sequence_, generation, request });
        std::push_heap(queue.begin(), queue.end(), QueueOrder{});
    }
    // A job parked behind its lane is back in the queue; let a worker look at it.
    wake_.notify_one();
    longWake_.notify_all();
    return true;
}

bool BackgroundLoadService::Cancel(const BackgroundRequestHandle& request) {
    if (request == nullptr) {
        return false;
    }
    std::scoped_lock lock{ mutex_ };
    BackgroundRequestState expected = BackgroundRequestState::Queued;
    if (!request->state_.compare_exchange_strong(expected, BackgroundRequestState::Cancelled, std::memory_order_acq_rel)) {
        return false;
    }
    // No worker touches a request it did not take; drop what the work captured right away.
    request->job_ = {};
    request->transform_ = {};
    request->finishedAt_ = std::chrono::steady_clock::now();
    ++stats_.cancelled;
    return true;
}

BackgroundLaneId BackgroundLoadService::OpenLane(BackgroundJobClass jobClass, std::uint32_t concurrency) {
    std::scoped_lock lock{ mutex_ };
    const BackgroundLaneId id = nextLane_++;
    lanes_.emplace(id, Lane{ .jobClass = jobClass, .concurrency = std::max(concurrency, 1U) });
    return id;
}

void BackgroundLoadService::CloseLane(BackgroundLaneId lane) noexcept {
    static_cast<void>(CancelLane(lane));
    WaitForLane(lane);
    std::scoped_lock lock{ mutex_ };
    lanes_.erase(lane);
}

std::size_t BackgroundLoadService::CancelLane(BackgroundLaneId lane) noexcept {
    if (lane == kNoBackgroundLane) {
        return 0U;
    }
    std::scoped_lock lock{ mutex_ };
    std::size_t cancelled = 0U;
    const auto cancel = [&](QueueEntry& entry) {
        BackgroundRequestState expected = BackgroundRequestState::Queued;
        if (entry.request->state_.compare_exchange_strong(expected, BackgroundRequestState::Cancelled, std::memory_order_acq_rel)) {
            entry.request->job_ = {};
            entry.request->finishedAt_ = std::chrono::steady_clock::now();
            ++stats_.cancelled;
            ++cancelled;
        }
    };
    const auto ofLane = [lane](const QueueEntry& entry) {
        return entry.request->lane_ == lane;
    };
    for (std::vector<QueueEntry>* queue : { &jobs_, &longJobs_ }) {
        for (QueueEntry& entry : *queue) {
            if (ofLane(entry)) {
                cancel(entry);
            }
        }
        if (std::erase_if(*queue, ofLane) > 0U) {
            std::make_heap(queue->begin(), queue->end(), QueueOrder{});
        }
    }
    if (const auto found = lanes_.find(lane); found != lanes_.end()) {
        for (QueueEntry& entry : found->second.parked) {
            cancel(entry);
        }
        found->second.parked.clear();
    }
    return cancelled;
}

void BackgroundLoadService::WaitForLane(BackgroundLaneId lane) noexcept {
    if (lane == kNoBackgroundLane) {
        return;
    }
    std::unique_lock lock{ mutex_ };
    laneIdle_.wait(lock, [this, lane] {
        const auto found = lanes_.find(lane);
        return found == lanes_.end() || found->second.running == 0U;
    });
}

void BackgroundLoadService::Forget(const std::filesystem::path& path) {
    std::scoped_lock lock{ filesMutex_ };
    files_.erase(path.native());
}

BackgroundLoadServiceStats BackgroundLoadService::Stats() const {
    std::scoped_lock lock{ mutex_ };
    return stats_;
}

bool BackgroundLoadService::IsLive(const QueueEntry& entry) noexcept {
    return entry.request->State() == BackgroundRequestState::Queued &&
        entry.generation == entry.request->generation_.load(std::memory_order_relaxed);
}

std::size_t BackgroundLoadService::QueuedCount() const {
    std::scoped_lock lock{ mutex_ };
    std::size_t count = static_cast<std::size_t>(std::ranges::count_if(reads_, IsLive)) +
        static_cast<std::size_t>(std::ranges::count_if(jobs_, IsLive)) +
        static_cast<std::size_t>(std::ranges::count_if(longJobs_, IsLive));
    for (const auto& [id, lane] : lanes_) {
        static_cast<void>(id);
        count += static_cast<std::size_t>(std::ranges::count_if(lane.parked, IsLive));
    }
    return count;
}

std::uint32_t BackgroundLoadService::LongWorkerCount() const {
    std::scoped_lock lock{ mutex_ };
    return static_cast<std::uint32_t>(longWorkers_.size());
}

bool BackgroundLoadService::WaitUntilDone(const BackgroundRequestHandle& request, std::chrono::steady_clock::time_point deadline) {
    while (request != nullptr && !request->IsDone()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds{ 100 });
    }
    return request != nullptr;
}

std::vector<BackgroundRequestHandle> BackgroundLoadService::TakeBatch(std::size_t count) {
    std::vector<BackgroundRequestHandle> batch;
    std::unique_lock lock{ mutex_ };
    for (;;) {
        if (stopping_) {
            return batch;
        }
        // A runnable job first while under the job limit: reads keep the other workers, and a
        // steady stream of reads cannot starve the jobs.
        if (jobsRunning_ < options_.jobWorkers) {
            if (BackgroundRequestHandle job = TakeJob(jobs_)) {
                ++jobsRunning_;
                stats_.peakJobsRunning = std::max(stats_.peakJobsRunning, jobsRunning_);
                batch.push_back(std::move(job));
                return batch;
            }
        }
        while (!reads_.empty() && batch.size() < count) {
            std::pop_heap(reads_.begin(), reads_.end(), QueueOrder{});
            QueueEntry entry = std::move(reads_.back());
            reads_.pop_back();
            if (entry.generation != entry.request->generation_.load(std::memory_order_relaxed)) {
                continue;
            }
            BackgroundRequestState expected = BackgroundRequestState::Queued;
            if (entry.request->state_.compare_exchange_strong(expected, BackgroundRequestState::InFlight, std::memory_order_acq_rel)) {
                batch.push_back(std::move(entry.request));
            }
        }
        if (!batch.empty()) {
            return batch;
        }
        wake_.wait(lock, [this] {
            return stopping_ || !reads_.empty() || (!jobs_.empty() && jobsRunning_ < options_.jobWorkers);
        });
    }
}

BackgroundRequestHandle BackgroundLoadService::TakeJob(std::vector<QueueEntry>& queue) {
    while (!queue.empty()) {
        std::pop_heap(queue.begin(), queue.end(), QueueOrder{});
        QueueEntry entry = std::move(queue.back());
        queue.pop_back();
        if (!IsLive(entry)) {
            continue;
        }
        Lane* lane = nullptr;
        if (entry.request->lane_ != kNoBackgroundLane) {
            const auto found = lanes_.find(entry.request->lane_);
            lane = found == lanes_.end() ? nullptr : &found->second;
            if (lane != nullptr && lane->running >= lane->concurrency) {
                lane->parked.push_back(std::move(entry));
                continue;
            }
        }
        BackgroundRequestState expected = BackgroundRequestState::Queued;
        if (!entry.request->state_.compare_exchange_strong(expected, BackgroundRequestState::InFlight, std::memory_order_acq_rel)) {
            continue;
        }
        if (lane != nullptr) {
            ++lane->running;
        }
        return std::move(entry.request);
    }
    return nullptr;
}

void BackgroundLoadService::LongWorkerLoop() {
    std::unique_lock lock{ mutex_ };
    for (;;) {
        if (stopping_) {
            return;
        }
        if (BackgroundRequestHandle job = TakeJob(longJobs_)) {
            ++longJobsRunning_;
            stats_.peakLongJobsRunning = std::max(stats_.peakLongJobsRunning, longJobsRunning_);
            lock.unlock();
            RunJob(job);
            job.reset();
            lock.lock();
            continue;
        }
        longWake_.wait(lock, [this] { return stopping_ || !longJobs_.empty(); });
    }
}

void BackgroundLoadService::WorkerLoop() {
    for (;;) {
        std::vector<BackgroundRequestHandle> batch = TakeBatch(options_.requestsInFlightPerWorker);
        if (batch.empty()) {
            return;
        }
        if (batch.front()->isJob_) {
            RunJob(batch.front());
            continue;
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

void BackgroundLoadService::RunJob(const BackgroundRequestHandle& request) {
    bool succeeded = false;
    std::string error;
    try {
        succeeded = request->job_(error);
    } catch (const std::exception& exception) {
        error = exception.what();
        succeeded = false;
    } catch (...) {
        error = "the job threw a non-standard exception";
        succeeded = false;
    }
    if (!succeeded) {
        request->error_ = error.empty() ? "the job failed" : std::move(error);
    }
    // What the job captured goes before its lane reads as idle: an owner waiting on the lane may
    // tear down what the captures refer to as soon as it wakes.
    request->job_ = {};
    request->finishedAt_ = std::chrono::steady_clock::now();
    {
        std::scoped_lock lock{ mutex_ };
        if (succeeded) {
            ++stats_.completed;
        } else {
            ++stats_.failed;
        }
        if (request->jobClass_ == BackgroundJobClass::Long) {
            ++stats_.longJobsRun;
            --longJobsRunning_;
        } else {
            ++stats_.jobsRun;
            --jobsRunning_;
        }
        if (request->lane_ != kNoBackgroundLane) {
            if (const auto found = lanes_.find(request->lane_); found != lanes_.end()) {
                Lane& lane = found->second;
                --lane.running;
                std::vector<QueueEntry>& queue = JobQueue(lane.jobClass);
                for (QueueEntry& parked : lane.parked) {
                    queue.push_back(std::move(parked));
                    std::push_heap(queue.begin(), queue.end(), QueueOrder{});
                }
                lane.parked.clear();
            }
        }
        request->state_.store(succeeded ? BackgroundRequestState::Completed : BackgroundRequestState::Failed,
            std::memory_order_release);
    }
    wake_.notify_all();
    longWake_.notify_all();
    laneIdle_.notify_all();
}

std::shared_ptr<BackgroundLoadService::FileHandle> BackgroundLoadService::AcquireFile(
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

void BackgroundLoadService::ServeBatch(std::vector<BackgroundRequestHandle>& batch) {
    struct Pending {
        BackgroundRequestHandle request;
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
    for (BackgroundRequestHandle& request : batch) {
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
        BackgroundRequest& request = *read.request;
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

void BackgroundLoadService::Finish(const BackgroundRequestHandle& request, bool succeeded) {
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
    request->state_.store(succeeded ? BackgroundRequestState::Completed : BackgroundRequestState::Failed, std::memory_order_release);
}

BackgroundLane::BackgroundLane(
    std::shared_ptr<BackgroundLoadService> service,
    BackgroundJobClass jobClass,
    std::uint32_t concurrency)
    : service_{ std::move(service) },
      id_{ service_->OpenLane(jobClass, concurrency) } {}

BackgroundLane::~BackgroundLane() {
    service_->CloseLane(id_);
}

BackgroundRequestHandle BackgroundLane::Run(BackgroundJob job, BackgroundPriority priority) {
    return service_->Run(std::move(job), priority, id_);
}

void BackgroundLane::CancelAndWait() noexcept {
    static_cast<void>(service_->CancelLane(id_));
    service_->WaitForLane(id_);
}

} // namespace kb::assets::streaming
