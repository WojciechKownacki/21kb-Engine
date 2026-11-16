#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

// The text messages two peers exchange to set up a WebRTC connection: "offer\n<sdp>", "answer\n<sdp>"
// and "candidate\n<mid>\n<candidate>". A signaling channel moves them between the peers in order; the
// connection itself never goes through it, so it can close once the data channels are open.
namespace kb::net {

enum class SignalingStatus : std::uint8_t {
    Ok,         // `message` holds the next message
    WouldBlock, // nothing to receive right now
    Closed,     // the channel is closed and everything it received has been read
};

// Send and Receive may be called from different threads, and Send from a library thread.
class ISignalingChannel {
public:
    ISignalingChannel() = default;
    ISignalingChannel(const ISignalingChannel&) = delete;
    ISignalingChannel& operator=(const ISignalingChannel&) = delete;
    virtual ~ISignalingChannel() = default;

    // False when the channel is closed or its queue is full.
    [[nodiscard]] virtual bool Send(std::string message) = 0;
    [[nodiscard]] virtual SignalingStatus Receive(std::string& message) = 0;
    virtual void Close() = 0;
};

// Two connected in-process channels, for tests and for peers that share a process.
[[nodiscard]] std::pair<std::unique_ptr<ISignalingChannel>, std::unique_ptr<ISignalingChannel>> CreateSignalingPair();

// A WebSocket client for signaling. Native builds connect with a ws:// URL; the browser build with
// ws:// or wss://. The channel opens asynchronously: Send queues until the socket is up.
[[nodiscard]] std::unique_ptr<ISignalingChannel> CreateWebSocketSignalingClient(const std::string& url, std::string& error);

// Where a server's WebSocket listener hands the signaling channels of arriving peers.
class SignalingListener final {
public:
    void Push(std::unique_ptr<ISignalingChannel> channel) {
        std::lock_guard lock(mutex_);
        if (pending_.size() < kMaxPending) pending_.push_back(std::move(channel));
    }
    [[nodiscard]] std::unique_ptr<ISignalingChannel> Pop() {
        std::lock_guard lock(mutex_);
        if (pending_.empty()) return nullptr;
        auto channel = std::move(pending_.front());
        pending_.pop_front();
        return channel;
    }

private:
    static constexpr std::size_t kMaxPending = 64U;
    std::mutex mutex_;
    std::deque<std::unique_ptr<ISignalingChannel>> pending_;
};

} // namespace kb::net
