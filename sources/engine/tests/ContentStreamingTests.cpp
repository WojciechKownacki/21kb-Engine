#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/assets/streaming/AsyncFileReader.hpp"
#include "engine/assets/streaming/StreamingResidency.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #include <process.h>
    #include <winioctl.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace kb::tests {
namespace {

namespace streaming = kb::assets::streaming;

using Clock = std::chrono::steady_clock;

[[nodiscard]] std::filesystem::path Root() {
#if defined(_WIN32)
    const long long process = static_cast<long long>(_getpid());
#else
    const long long process = static_cast<long long>(getpid());
#endif
    return std::filesystem::temp_directory_path() / ("21kb_content_streaming_" + std::to_string(process));
}

void Purge(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
}

[[nodiscard]] std::vector<std::uint8_t> Noise(std::uint64_t seed, std::size_t count) {
    std::vector<std::uint8_t> bytes(count);
    std::uint64_t state = seed * 0x9E3779B97F4A7C15ULL + 1U;
    for (std::uint8_t& value : bytes) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        value = static_cast<std::uint8_t>((state >> 33U) & 0xFFU);
    }
    return bytes;
}

void WriteFileBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// ---- Asynchronous I/O -------------------------------------------------------------------------

// Red when: queued reads are not served highest priority first, a re-prioritised read keeps its
// old place, a cancelled read runs, reads come back with the wrong bytes at unaligned offsets,
// or a worker does not keep several reads in flight.
void AsyncReadsFollowPriorityAndReturnExactRanges() {
    const std::filesystem::path root = Root() / "io";
    Purge(root);
    std::filesystem::create_directories(root);
    const std::filesystem::path path = root / "data.bin";
    const std::vector<std::uint8_t> content = Noise(41U, 3U * 1024U * 1024U + 777U);
    WriteFileBytes(path, content);

    streaming::AsyncFileReader io{ streaming::AsyncFileReaderOptions{ .workerCount = 1U, .requestsInFlightPerWorker = 4U } };
    std::mutex orderMutex;
    std::vector<int> order;
    std::atomic<bool> gateOpen{ false };
    std::atomic<bool> gateEntered{ false };
    const streaming::AsyncReadHandle gate = io.Read(path, 0U, 16U, 1000,
        [&](std::vector<std::uint8_t>&, std::string&) {
            gateEntered = true;
            const Clock::time_point deadline = Clock::now() + std::chrono::seconds{ 30 };
            while (!gateOpen && Clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
            }
            return true;
        });
    const Clock::time_point enterDeadline = Clock::now() + std::chrono::seconds{ 30 };
    while (!gateEntered && Clock::now() < enterDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
    }
    Require(gateEntered, "The I/O worker never started the gate read");

    const auto tagged = [&](int tag) {
        return [&, tag](std::vector<std::uint8_t>&, std::string&) {
            std::scoped_lock lock{ orderMutex };
            order.push_back(tag);
            return true;
        };
    };
    std::vector<streaming::AsyncReadHandle> requests;
    const std::array<int, 6U> priorities{ 1, 5, 3, 10, 2, 4 };
    for (std::size_t index = 0U; index < priorities.size(); ++index) {
        requests.push_back(io.Read(path, index * 4096U, 4096U, priorities[index], tagged(priorities[index])));
    }
    Require(io.QueuedCount() == priorities.size(), "Queued reads are not all waiting behind the gate");
    Require(io.Reprioritize(requests[0], 20), "A queued read could not be re-prioritised");   // 1 -> first
    Require(io.Cancel(requests[4]), "A queued read could not be cancelled");                  // 2 never runs
    Require(!io.Cancel(gate), "A read already in flight was cancelled");
    gateOpen = true;
    for (const streaming::AsyncReadHandle& request : requests) {
        Require(streaming::AsyncFileReader::WaitUntilDone(request, Clock::now() + std::chrono::seconds{ 30 }),
            "A queued read never finished");
    }
    Require(order == std::vector<int>{ 1, 10, 5, 4, 3 }, "Queued reads were not served by priority");
    Require(requests[4]->State() == streaming::AsyncReadState::Cancelled, "A cancelled read did not stay cancelled");
    Require(io.Stats().peakInFlight >= 4U, "The worker did not keep several reads in flight");

    // Exact ranges at awkward offsets, through the unbuffered path where the platform has one.
    std::vector<streaming::AsyncReadHandle> ranges;
    const std::array<std::pair<std::uint64_t, std::uint64_t>, 6U> windows{ {
        { 0U, 1U }, { 1U, 511U }, { 4095U, 2U }, { 12345U, 99999U }, { content.size() - 3U, 3U }, { 700000U, 1500000U },
    } };
    for (const auto& [offset, length] : windows) {
        ranges.push_back(io.Read(path, offset, length, 0));
    }
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        Require(streaming::AsyncFileReader::WaitUntilDone(ranges[index], Clock::now() + std::chrono::seconds{ 30 }) &&
                ranges[index]->State() == streaming::AsyncReadState::Completed,
            ("An exact-range read failed: " + ranges[index]->Error()).c_str());
        const auto [offset, length] = windows[index];
        Require(std::equal(ranges[index]->Bytes().begin(), ranges[index]->Bytes().end(),
                    content.begin() + static_cast<std::ptrdiff_t>(offset)) &&
                ranges[index]->Bytes().size() == length,
            "An asynchronous read returned the wrong bytes");
    }
    const streaming::AsyncReadHandle pastEnd = io.Read(path, content.size() - 10U, 20U, 0);
    Require(streaming::AsyncFileReader::WaitUntilDone(pastEnd, Clock::now() + std::chrono::seconds{ 30 }) &&
            pastEnd->State() == streaming::AsyncReadState::Failed && pastEnd->Bytes().empty(),
        "A read past the end of the file succeeded");
    const streaming::AsyncReadHandle missing = io.Read(root / "missing.bin", 0U, 1U, 0);
    Require(streaming::AsyncFileReader::WaitUntilDone(missing, Clock::now() + std::chrono::seconds{ 30 }) &&
            missing->State() == streaming::AsyncReadState::Failed,
        "A read of a missing file succeeded");
#if defined(_WIN32)
    Require(io.Stats().unbufferedReads > 0U, "No read took the unbuffered path on Windows");
#endif
    io.Forget(path);
    Purge(root);
}

// ---- Residency under a budget -----------------------------------------------------------------

// Red when: resident plus in-flight bytes ever exceed the budget; when loads do not go highest
// priority and coarsest first; when space is not made by evicting levels nobody wants before
// wanted levels of less important resources; when a load evicts something as important as
// itself; or when a smaller budget is not honoured at once.
void StreamingStaysWithinItsBudget() {
    namespace s = streaming;
    // Two resources, 4 levels each: 1000, 250, 60, 15 bytes; the 15-byte level is the floor.
    const s::StreamingResourceDesc desc{ .levelBytes = { 1000U, 250U, 60U, 15U }, .residentFloor = 3U };
    {
        s::StreamingResidencyManager manager{ 30U + 1000U + 250U + 60U };
        Require(manager.Register(1U, desc) && manager.Register(2U, desc) && !manager.Register(1U, desc),
            "Resources did not register exactly once");
        Require(manager.Stats().residentBytes == 30U && manager.Stats().floorBytes == 30U,
            "Floors were not resident at registration");
        manager.Request(1U, 0U, 1.0F, 1U);
        manager.Request(2U, 0U, 0.1F, 1U);
        s::StreamingPlan plan = manager.Plan(1U);
        // Priority first, coarse first: 1's 60, 250 and 1000, then nothing of 2's fits.
        Require(plan.loads.size() == 3U && plan.loads[0].resource == 1U && plan.loads[0].level == 2U &&
                plan.loads[1].level == 1U && plan.loads[2].level == 0U,
            "Loads were not ordered by priority and coarseness");
        Require(manager.Stats().residentBytes + manager.Stats().inFlightBytes <= manager.Budget(),
            "Starting loads exceeded the budget");
        // The fine level lands first; it must wait for the coarser ones.
        manager.CompleteLoad(1U, 0U, true, 2U);
        Require(manager.ResidentLevel(1U) == 3U, "A level became resident before the coarser ones");
        manager.CompleteLoad(1U, 2U, true, 2U);
        manager.CompleteLoad(1U, 1U, true, 2U);
        Require(manager.ResidentLevel(1U) == 0U, "Arrived levels were not promoted in order");

        // Resource 2 is now more important: the load evicts 1's finer levels, finest first.
        manager.Request(1U, 0U, 0.2F, 3U);
        manager.Request(2U, 2U, 0.9F, 3U);
        plan = manager.Plan(3U);
        Require(plan.loads.size() == 1U && plan.loads[0].resource == 2U && plan.loads[0].level == 2U &&
                plan.evictions.size() == 1U && plan.evictions[0].resource == 1U && plan.evictions[0].newResidentLevel == 1U,
            "A more important load did not evict the least important finest level");
        manager.CompleteLoad(2U, 2U, true, 3U);

        // Equal importance never evicts: no thrashing between two resources.
        manager.Request(1U, 0U, 0.5F, 4U);
        manager.Request(2U, 0U, 0.5F, 4U);
        plan = manager.Plan(4U);
        Require(plan.evictions.empty(), "A load evicted a resource as important as itself");
        for (const s::StreamingLoad& load : plan.loads) {
            manager.CompleteLoad(load.resource, load.level, true, 4U);
        }
        Require(manager.Stats().residentBytes <= manager.Budget(), "Equal-priority loads exceeded the budget");

        // Nobody wants 1's fine levels any more: they go before anything wanted.
        manager.Request(1U, 3U, 0.9F, 5U);
        manager.Request(2U, 0U, 0.1F, 5U);
        plan = manager.Plan(5U);
        Require(!plan.evictions.empty() && plan.evictions.front().resource == 1U,
            "Unwanted levels were not the first to go");

        // A smaller budget is honoured before anything starts.
        for (const s::StreamingLoad& load : plan.loads) {
            manager.CompleteLoad(load.resource, load.level, true, 5U);
        }
        manager.SetBudget(30U + 60U);
        static_cast<void>(manager.Plan(6U));
        Require(manager.Stats().residentBytes + manager.Stats().inFlightBytes <= 90U, "A shrunken budget was not honoured");
        manager.Unregister(1U);
        manager.Unregister(2U);
        Require(manager.Stats().residentBytes == 0U && manager.Stats().inFlightBytes == 0U,
            "Unregistering left bytes accounted");
    }

    // A long randomized run with asynchronous completion: the budget holds every frame, and
    // what is resident is what matters most.
    {
        constexpr std::uint64_t kBudget = 40U * 15U + 6000U;
        s::StreamingResidencyManager manager{ kBudget };
        for (std::uint64_t id = 1U; id <= 40U; ++id) {
            Require(manager.Register(id, desc), "A resource did not register");
        }
        std::vector<std::pair<std::uint64_t, s::StreamingLoad>> inFlight;
        std::uint64_t random = 0x1234567U;
        const auto next = [&random] {
            random = random * 6364136223846793005ULL + 1442695040888963407ULL;
            return random >> 33U;
        };
        for (std::uint64_t frame = 1U; frame <= 2000U; ++frame) {
            for (std::uint64_t id = 1U; id <= 40U; ++id) {
                if (next() % 4U == 0U) {
                    manager.Request(id, static_cast<std::uint32_t>(next() % 4U), static_cast<float>(next() % 1000U) / 1000.0F, frame);
                }
            }
            const s::StreamingPlan plan = manager.Plan(frame, { .maxLoadsInFlight = 8U });
            for (const s::StreamingLoad& load : plan.loads) {
                inFlight.emplace_back(frame + 1U + next() % 3U, load);
            }
            for (auto pending = inFlight.begin(); pending != inFlight.end();) {
                if (pending->first <= frame) {
                    manager.CompleteLoad(pending->second.resource, pending->second.level, next() % 50U != 0U, frame);
                    pending = inFlight.erase(pending);
                } else {
                    ++pending;
                }
            }
            const s::StreamingResidencyStats stats = manager.Stats();
            Require(stats.residentBytes + stats.inFlightBytes <= kBudget, "The streaming budget was exceeded");
        }
        const s::StreamingResidencyStats stats = manager.Stats();
        Require(stats.peakCommittedBytes <= kBudget && stats.loadsCompleted > 100U && stats.evictions > 10U,
            "The randomized run did not exercise loads and evictions");
        std::cout << "content-streaming: budget " << kBudget << " bytes, peak committed " << stats.peakCommittedBytes
                  << " bytes over 2000 frames, " << stats.loadsCompleted << " loads, " << stats.evictions << " evictions\n";
    }

    // Floors alone over budget: nothing streams, and the stats say why.
    {
        s::StreamingResidencyManager manager{ 20U };
        Require(manager.Register(1U, desc) && manager.Register(2U, desc), "Floor-only resources did not register");
        manager.Request(1U, 0U, 1.0F, 1U);
        const s::StreamingPlan plan = manager.Plan(1U);
        Require(plan.loads.empty() && manager.Stats().floorBytes > manager.Budget(),
            "A load started although the floors alone exceed the budget");
    }
}

} // namespace

void RunContentStreamingTests() {
    AsyncReadsFollowPriorityAndReturnExactRanges();
    StreamingStaysWithinItsBudget();
    Purge(Root());
}

} // namespace kb::tests
