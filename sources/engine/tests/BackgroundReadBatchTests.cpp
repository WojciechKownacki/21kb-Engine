#include "engine/assets/streaming/BackgroundLoadService.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace io = kb::assets::streaming;
using Clock = std::chrono::steady_clock;

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void Wait(const io::BackgroundRequestHandle& request, io::BackgroundRequestState expected) {
    Require(io::BackgroundLoadService::WaitUntilDone(request, Clock::now() + std::chrono::seconds{ 30 }),
        "background read did not finish");
    Require(request->State() == expected, request->Error().c_str());
}
} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("21kb_background_reads_" + std::to_string(Clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto path = root / "Ranges.bin";
    std::vector<std::uint8_t> content(131077U);
    for (std::size_t i = 0U; i < content.size(); ++i) content[i] = static_cast<std::uint8_t>((i * 37U + i / 4096U) & 255U);
    {
        std::ofstream output{ path, std::ios::binary };
        output.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
        Require(output.good(), "could not write owned read fixture");
    }
    std::atomic<bool> entered{ false };
    std::atomic<bool> released{ false };
    std::vector<int> order;
    {
        io::BackgroundLoadService service{ { .workerCount = 1U, .requestsInFlightPerWorker = 4U } };
        const auto gate = service.Read(path, 0U, 1U, 1000, [&](auto&, auto&) {
            entered.store(true);
            const auto deadline = Clock::now() + std::chrono::seconds{ 30 };
            while (!released.load() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
            return released.load();
        });
        const auto deadline = Clock::now() + std::chrono::seconds{ 30 };
        while (!entered.load() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
        Require(entered.load(), "read worker did not enter the gate");
        std::vector<io::BackgroundRequestHandle> requests;
        constexpr std::array priorities{ 1, 5, 3, 10, 2, 4 };
        for (std::size_t i = 0U; i < priorities.size(); ++i) {
            requests.push_back(service.Read(path, i * 4096U + 3U, 4099U, priorities[i], [&, i](auto&, auto&) {
                order.push_back(priorities[i]);
                return true;
            }));
        }
        Require(service.Reprioritize(requests[0], 20), "queued reprioritization failed");
        Require(service.Cancel(requests[4]), "queued cancellation failed");
        Require(!service.Cancel(gate), "an active request was cancelled");
        released.store(true);
        Wait(gate, io::BackgroundRequestState::Completed);
        for (std::size_t i = 0U; i < requests.size(); ++i) {
            Wait(requests[i], i == 4U ? io::BackgroundRequestState::Cancelled : io::BackgroundRequestState::Completed);
            if (i == 4U) continue;
            Require(requests[i]->Bytes().size() == 4099U && std::equal(requests[i]->Bytes().begin(),
                    requests[i]->Bytes().end(), content.begin() + static_cast<std::ptrdiff_t>(i * 4096U + 3U)),
                "native read returned an incorrect range");
        }
        Require(order == std::vector<int>{ 1, 10, 5, 4, 3 }, "read publication order changed");
        const auto batchStats = service.Stats();
        Require(batchStats.peakInFlight >= 4U, "worker did not admit the configured batch");
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__APPLE__)
        Require(batchStats.asynchronousReads >= 6U && batchStats.peakSubmittedReadBatch >= 4U,
            "native API did not accept a complete batch before the first wait");
#endif
        const auto memory = service.ReadMemory(content, 7U, 11U, 0);
        Wait(memory, io::BackgroundRequestState::Completed);
        Require(memory->Bytes().size() == 11U && std::equal(memory->Bytes().begin(), memory->Bytes().end(), content.begin() + 7),
            "memory read changed bytes");
        Require(service.Stats().asynchronousReads == batchStats.asynchronousReads, "memory copy counted as native I/O");
        Wait(service.Read(path, content.size(), 0U, 0), io::BackgroundRequestState::Completed);
        const auto shortRead = service.Read(path, content.size() - 3U, 11U, 0);
        Wait(shortRead, io::BackgroundRequestState::Failed);
        Require(shortRead->Bytes().empty(), "short native read exposed partial bytes");
        Wait(service.Read(root / "Missing.bin", 0U, 1U, 0), io::BackgroundRequestState::Failed);
        Wait(service.Read(path, std::numeric_limits<std::uint64_t>::max() - 1U, 3U, 0), io::BackgroundRequestState::Failed);
#if !defined(_WIN32)
        Wait(service.Read(path, std::numeric_limits<std::uint64_t>::max(), 0U, 0), io::BackgroundRequestState::Failed);
#endif
        const auto decoded = service.Read(path, 0U, 8U, 0, [](auto&, auto& error) {
            error = "fixture decode rejection";
            return false;
        });
        Wait(decoded, io::BackgroundRequestState::Failed);
        Require(decoded->Bytes().empty() && decoded->Error() == "fixture decode rejection", "transform failure was lost");
        service.Forget(path);
        std::cout << "background_read_batch native_reads=" << batchStats.asynchronousReads
                  << " submitted_batch=" << batchStats.peakSubmittedReadBatch << " priority_and_ranges=passed" << std::endl;
    }
    std::filesystem::remove_all(root);
    return EXIT_SUCCESS;
}
