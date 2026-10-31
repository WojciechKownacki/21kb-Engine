#include "engine/script/LuaDebugFileTransport.hpp"
#include <atomic>
#include <fstream>
#include <thread>
#include <vector>
#include <algorithm>
#include <random>

namespace kb::script {
namespace {
using kb::core::JsonValue;
JsonValue Failure(std::string error) {
    auto result = JsonValue::MakeObject();
    result.Set("schema", JsonValue::MakeString("21kb.lua-debug/v1"));
    result.Set("succeeded", JsonValue::MakeBool(false));
    result.Set("error", JsonValue::MakeString(std::move(error)));
    return result;
}
bool Write(const std::filesystem::path& path, std::string_view text) {
    const auto temporary = std::filesystem::path{path.string() + ".tmp"};
    { std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      output.write(text.data(), static_cast<std::streamsize>(text.size()));
      if (!output.good()) return false; }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    const bool succeeded = !error;
    if (!succeeded) std::filesystem::remove(temporary, error);
    return succeeded;
}
bool Read(const std::filesystem::path& path, std::size_t limit, std::string& value) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > limit) return false;
    value.resize(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    input.read(value.data(), static_cast<std::streamsize>(value.size()));
    return input.good();
}
bool RequestName(const std::filesystem::path& path) {
    const auto name = path.stem().string();
    return path.extension() == ".request" && !name.empty() && name.size() <= 64U &&
        std::all_of(name.begin(), name.end(), [](char c) { return (c >= '0' && c <= '9') || c == '-'; });
}
}
LuaDebugFileServer::~LuaDebugFileServer() { Stop(); }
bool LuaDebugFileServer::Start(const std::filesystem::path& directory, std::string& error, std::chrono::milliseconds lease) {
    if (!directory_.empty() || directory.empty() || lease.count() < 100 || lease.count() > 60000) {
        error = "invalid or already active debug endpoint"; return false;
    }
    std::error_code ec;
    if (!std::filesystem::create_directory(directory, ec)) {
        error = "debug endpoint must be a new directory: " + ec.message(); return false;
    }
    directory_ = directory;
    lease_ = lease;
    lastRequest_ = std::chrono::steady_clock::now();
    if (!Write(directory_ / "endpoint.json", "{\"schema\":\"21kb.lua-debug/v1\"}")) {
        Stop(); error = "cannot publish debug endpoint"; return false;
    }
    error.clear();
    return true;
}
void LuaDebugFileServer::Poll() {
    if (directory_.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    if (session_.Connected() && now - lastRequest_ >= lease_) session_.Disconnect();
    std::error_code error;
    std::vector<std::filesystem::path> requests;
    unsigned examined = 0U;
    for (std::filesystem::directory_iterator it(directory_, error), end; !error && it != end && examined < 64U; it.increment(error), ++examined) {
        if (RequestName(it->path()) && it->is_regular_file(error)) requests.push_back(it->path());
        if (requests.size() == 8U) break;
    }
    if (error) { session_.Disconnect(); return; }
    std::sort(requests.begin(), requests.end());
    for (const auto& path : requests) {
        std::string request;
        auto reply = Read(path, 65536U, request) ? session_.Execute(request) : Failure("cannot read bounded debug request");
        lastRequest_ = now;
        const auto response = directory_ / (path.stem().string() + ".response");
        auto text = reply.Dump();
        if (text.size() > 8U * 1024U * 1024U) text = Failure("debug response exceeds limit").Dump();
        if (Write(response, text)) std::filesystem::remove(path, error);
    }
}
void LuaDebugFileServer::Stop() noexcept {
    session_.Disconnect();
    if (directory_.empty()) return;
    std::error_code error;
    std::filesystem::remove(directory_ / "endpoint.json", error);
    std::filesystem::remove(directory_, error); // Only removes an empty endpoint.
    directory_.clear();
}
JsonValue SendLuaDebugFileRequest(const std::filesystem::path& directory, std::string_view request, std::chrono::milliseconds timeout) {
    if (request.size() > 65536U || timeout.count() < 1 || timeout.count() > 60000) return Failure("invalid request size or timeout");
    std::error_code error;
    if (!std::filesystem::is_regular_file(directory / "endpoint.json", error)) return Failure("debug endpoint is unavailable");
    static std::atomic<unsigned long long> sequence{0};
    const auto id = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(sequence.fetch_add(1U)) + "-" + std::to_string(std::random_device{}());
    const auto path = directory / (id + ".request");
    const auto response = directory / (id + ".response");
    if (!Write(path, request)) return Failure("cannot submit debug request");
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        std::string text;
        if (Read(response, 8U * 1024U * 1024U, text)) {
            std::filesystem::remove(response, error);
            JsonValue reply;
            std::string parseError;
            if (!JsonValue::Parse(text, reply, parseError)) return Failure("invalid debug response");
            return reply;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    std::filesystem::remove(path, error);
    std::filesystem::remove(response, error);
    return Failure("debug request timed out");
}
} // namespace kb::script
