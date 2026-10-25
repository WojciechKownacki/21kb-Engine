#include "engine/assets/streaming/BackgroundLoadService.hpp"
#include "WebDownloadProgress.hpp"
#include "WebDownloadBuffer.hpp"

#include <emscripten/emscripten.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace kb::assets::streaming;
std::shared_ptr<BackgroundLoadService> service;
std::vector<BackgroundRequestHandle> requests;
BackgroundRequestHandle abandoned;
std::weak_ptr<BackgroundLoadService> releasedOwner;
std::vector<int> order;
std::array<std::uint8_t, 5> bytes{1, 2, 3, 4, 5};
bool ownerJobRan = false;
std::chrono::steady_clock::time_point deadline;
std::vector<BackgroundRequestHandle> fairnessReads;
BackgroundRequestHandle fairnessJob;
unsigned readsFinished = 0;
unsigned readsAtLongJob = 0;
unsigned allocations = 0;
unsigned releases = 0;
bool refuseAllocation = false;

void* AllocateDownload(void* pointer, std::size_t count) {
    ++allocations;
    return refuseAllocation ? nullptr : std::realloc(pointer, count);
}
void ReleaseDownload(void* pointer) {
    ++releases;
    std::free(pointer);
}

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        emscripten_force_exit(EXIT_FAILURE);
    }
}

void Finish(void*) {
    Require(abandoned->State() == BackgroundRequestState::Cancelled,
        "A callback used a service destroyed before its queued dispatch");
    Require(ownerJobRan && releasedOwner.expired(),
        "Dispatch did not retain its service until a job released the last external owner");
    std::puts("Web cooperative reads/jobs/priority/cancel/drain/wait/lifetime passed");
    emscripten_force_exit(EXIT_SUCCESS);
}

void CheckFairness(void*) {
    Require(std::chrono::steady_clock::now() < deadline, "Queued work did not make progress");
    if (!fairnessJob->IsDone() || readsFinished != fairnessReads.size()) {
        emscripten_async_call(CheckFairness, nullptr, 0);
        return;
    }
    for (const auto& read : fairnessReads) {
        Require(read->State() == BackgroundRequestState::Completed && read->Bytes() == std::vector<std::uint8_t>({1}),
            "Fair dispatch lost or changed a queued read");
    }
    std::printf("Long work dispatched after %u of %zu queued reads\n", readsAtLongJob, fairnessReads.size());
    Require(readsAtLongJob < fairnessReads.size(), "Long work waited for the entire read backlog");
    service.reset();
    emscripten_async_call(Finish, nullptr, 1);
}

void CheckComplete(void*) {
    Require(std::chrono::steady_clock::now() < deadline, "Cooperative requests did not finish");
    for (const auto& request : requests) {
        if (!request->IsDone()) {
            emscripten_async_call(CheckComplete, nullptr, 0);
            return;
        }
    }
    Require(order == std::vector<int>({1, 2, 3}), "Priority or long-job dispatch order differs");
    Require(requests[2]->Bytes() == std::vector<std::uint8_t>({12, 3, 4}),
        "Memory-backed read or transform differs");
    Require(requests[3]->Bytes() == std::vector<std::uint8_t>({2, 3, 4}), "Virtual file read differs");
    Require(requests[4]->State() == BackgroundRequestState::Failed && !requests[4]->Error().empty(),
        "Out-of-range read did not fail");
    Require(requests[5]->State() == BackgroundRequestState::Failed && requests[5]->Error() == "expected refusal",
        "Transform failure did not preserve its diagnostic");
    {
        BackgroundLane lane{service};
        const auto job = lane.Run([](std::string&) { return true; });
        lane.Drain();
        Require(job->State() == BackgroundRequestState::Completed, "Lane drain did not execute queued work");
    }
    const auto waited = service->ReadMemory(bytes, 0, 1, 0);
    Require(BackgroundLoadService::WaitUntilDone(waited, deadline) && waited->Bytes() == std::vector<std::uint8_t>({1}),
        "Explicit blocking wait did not pump the cooperative request");
    const auto stats = service->Stats();
    Require(service->WorkerCount() == 0 && service->LongWorkerCount() == 0 &&
        stats.peakInFlight == 1 && stats.asynchronousReads == 0 && stats.longJobsRun == 1,
        "Cooperative diagnostics claimed worker threads or native asynchronous file reads");
    fairnessJob = service->Run([](std::string&) {
        readsAtLongJob = readsFinished;
        return true;
    }, 0, BackgroundJobClass::Long);
    for (unsigned index = 0; index < 32; ++index) {
        fairnessReads.push_back(service->ReadMemory(bytes, 0, 1, 0,
            [](std::vector<std::uint8_t>&, std::string&) { ++readsFinished; return true; }));
    }
    emscripten_async_call(CheckFairness, nullptr, 0);
}

void CheckYield(void*) {
    Require(order == std::vector<int>({1}) && !requests[1]->IsDone(),
        "Dispatch did not yield to the event loop between queued requests");
    emscripten_async_call(CheckComplete, nullptr, 0);
}
} // namespace

int main() {
    {
        using Buffer = kb::game::WebDownloadBuffer;
        Buffer buffer{128, AllocateDownload, ReleaseDownload};
        Require(buffer.Reserve(0) && allocations == 0, "An unknown length allocated before receiving bytes");
        Require(buffer.Reserve(4), "A known download length could not be reserved");
        auto* first = buffer.Append(4);
        Require(first != nullptr && allocations == 1 && buffer.Capacity() == 4,
            "A known length caused duplicate allocations");
        for (unsigned i = 0; i < 4; ++i) first[i] = static_cast<std::uint8_t>(i + 1);
        refuseAllocation = true;
        Require(buffer.Append(1) == nullptr && buffer.LastError() == Buffer::Error::Allocation &&
            buffer.Bytes().size() == 4 && buffer.Bytes()[3] == 4,
            "Allocation refusal lost received bytes or advanced the download");
        refuseAllocation = false;
        auto* second = buffer.Append(124);
        Require(second != nullptr && buffer.Bytes().size() == 128 && buffer.Bytes()[0] == 1 &&
            buffer.Bytes()[3] == 4, "Growing an unknown-length download changed its previous bytes");
        for (unsigned i = 0; i < 124; ++i) second[i] = static_cast<std::uint8_t>(i);
        const unsigned before = allocations;
        Require(buffer.Append(1) == nullptr && buffer.Append((std::numeric_limits<std::size_t>::max)()) == nullptr &&
            !buffer.Reserve(129) && allocations == before && buffer.LastError() == Buffer::Error::SizeLimit &&
            buffer.Bytes().size() == 128 && buffer.Bytes().back() == 123,
            "An oversized or overflowing download reached allocation or changed received bytes");
        buffer.Reset();
        buffer.Reset();
        Require(releases == 1 && buffer.Bytes().empty() && buffer.Capacity() == 0 &&
            buffer.LastError() == Buffer::Error::None, "Download cleanup did not release its allocation exactly once");
        Require(buffer.Append(3) != nullptr && buffer.Capacity() == 128,
            "A new unknown-length download could not reuse the bounded buffer");
    }
    Require(releases == 2, "A download buffer outlived its owner");
    {
        kb::game::WebDownloadBuffer buffer{128, AllocateDownload, ReleaseDownload};
        refuseAllocation = true;
        Require(!buffer.Reserve(64) && buffer.Bytes().empty() && buffer.Capacity() == 0 &&
            buffer.LastError() == kb::game::WebDownloadBuffer::Error::Allocation,
            "An initial allocation refusal left an invalid download buffer");
        refuseAllocation = false;
    }
    Require(releases == 2, "An allocation refusal tried to release an unowned buffer");
    std::puts("Web download allocation/size/overflow/ownership checks passed");
    {
        using namespace std::chrono_literals;
        kb::game::WebDownloadProgress progress;
        const kb::game::WebDownloadProgress::Clock::time_point start{};
        progress.Start(start);
        Require(progress.Remaining(start + 29999ms) == 1ms && progress.Remaining(start + 30s) == 0ms,
            "Download idle deadline boundary differs");
        progress.Observe(10, start + 29s);
        Require(progress.Remaining(start + 31s) == 28s,
            "Received bytes did not extend the idle deadline for a longer active transfer");
        progress.Observe(10, start + 50s);
        progress.Observe(9, start + 51s);
        Require(progress.Remaining(start + 59s) == 0ms,
            "Duplicate or regressing progress incorrectly extended the download deadline");
        progress.Start(start + 60s);
        Require(progress.Remaining(start + 60s) == 30s, "A new download retained the preceding deadline");
    }
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    service = BackgroundLoadService::Shared();
    const auto low = service->Run([](std::string&) { order.push_back(1); return true; }, 1);
    const auto high = service->Run([](std::string&) { order.push_back(2); return true; }, 5);
    Require(service->Reprioritize(low, 9), "Queued job could not be reprioritized");
    const auto cancelled = service->Run([](std::string&) { std::abort(); return false; }, 20);
    Require(service->Cancel(cancelled) && cancelled->State() == BackgroundRequestState::Cancelled,
        "Queued job could not be cancelled");
    std::ofstream{"ReadFixture.bin", std::ios::binary}.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    requests = {low, high,
        service->ReadMemory(bytes, 1, 3, 0, [](std::vector<std::uint8_t>& result, std::string&) { result[0] += 10; return true; }),
        service->Read("ReadFixture.bin", 1, 3, 0),
        service->ReadMemory(bytes, 10, 3, 0),
        service->ReadMemory(bytes, 0, 1, 0, [](std::vector<std::uint8_t>&, std::string& error) { error = "expected refusal"; return false; }),
        service->Run([](std::string&) { order.push_back(3); return true; }, 0, BackgroundJobClass::Long)};
    Require(low->State() == BackgroundRequestState::Queued && service->LongWorkerCount() == 0,
        "Submission executed inline or tried creating a worker");
    {
        BackgroundLoadService temporary;
        abandoned = temporary.Run([](std::string&) { std::abort(); return false; }, 0);
    }
    {
        auto owned = std::make_shared<BackgroundLoadService>();
        releasedOwner = owned;
        static_cast<void>(owned->Run([owner=owned](std::string&) { ownerJobRan = true; return owner->WorkerCount() == 0; }, 0));
    }
    emscripten_async_call(CheckYield, nullptr, 0);
    emscripten_exit_with_live_runtime();
}
