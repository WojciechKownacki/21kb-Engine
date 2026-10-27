#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <utility>

// Client side prediction with server reconciliation, and snapshot interpolation, for state the game
// declares and steps deterministically.
//
// The contract. The state and the input are the game's own types. `Step(state, input, dt)` must be a
// pure function of its arguments, the same on the client and on the server, for prediction to be exact;
// if the server also does something the client cannot know (a wall, another player), the prediction is
// wrong by that amount, Reconcile finds it and replays the inputs the server has not processed yet on top
// of the server's state, which converges as soon as the inputs stop. Providers that are not deterministic
// (a physics engine stepped with a different time step, for example) cannot be predicted this way; they
// are not hidden by this class: Reconcile reports how large each correction was.
//
// How it is used. The client calls Predict for each input it samples, sends the input with the returned
// sequence number to the server (reliably, in order), and the server applies inputs in order and
// replicates its state together with the sequence number of the last input it applied. When that state
// arrives the client calls Reconcile with it.
namespace kb::net {

template <typename State, typename Input>
class ClientPrediction final {
public:
    using StepFunction = std::function<State(const State&, const Input&, float)>;
    using DistanceFunction = std::function<double(const State&, const State&)>;

    struct Stats {
        std::uint64_t predicted = 0U;
        std::uint64_t reconciliations = 0U;
        std::uint64_t corrections = 0U;     // reconciliations that moved the state by more than the tolerance
        std::uint64_t replayedInputs = 0U;
        std::uint64_t rejectedAcks = 0U;    // acknowledgements of inputs that were never sent
        std::uint64_t refusedInputs = 0U;   // inputs refused because too many were unacknowledged
        double lastError = 0.0;
        double largestError = 0.0;
    };

    ClientPrediction(StepFunction step, DistanceFunction distance, State initial, float stepSeconds, double tolerance, std::size_t maxPending = 128U)
        : step_(std::move(step)), distance_(std::move(distance)), state_(std::move(initial)), stepSeconds_(stepSeconds), tolerance_(tolerance),
          maxPending_(maxPending) {}

    // Applies the input to the predicted state at once and remembers it until the server has processed it.
    // Returns its sequence number (1, 2, ...), or 0 when `maxPending` inputs are still unacknowledged; then
    // nothing is applied and the input must not be sent.
    [[nodiscard]] std::uint32_t Predict(const Input& input) {
        if (pending_.size() >= maxPending_) {
            ++stats_.refusedInputs;
            return 0U;
        }
        const std::uint32_t sequence = nextSequence_++;
        pending_.emplace_back(sequence, input);
        state_ = step_(state_, input, stepSeconds_);
        ++stats_.predicted;
        return sequence;
    }

    // `authoritative` is the server's state after it applied every input up to `lastProcessed`. Inputs up to
    // there are forgotten, the rest are replayed on top of it, and the predicted state becomes the result.
    // Returns the distance between the previous prediction and the result.
    double Reconcile(const State& authoritative, std::uint32_t lastProcessed) {
        if (lastProcessed >= nextSequence_) {
            ++stats_.rejectedAcks;
            return 0.0;
        }
        ++stats_.reconciliations;
        while (!pending_.empty() && pending_.front().first <= lastProcessed) {
            pending_.pop_front();
        }
        State replayed = authoritative;
        for (const auto& [sequence, input] : pending_) {
            replayed = step_(replayed, input, stepSeconds_);
            ++stats_.replayedInputs;
        }
        const double error = distance_(state_, replayed);
        stats_.lastError = error;
        stats_.largestError = std::max(stats_.largestError, error);
        if (error > tolerance_) {
            ++stats_.corrections;
        }
        state_ = std::move(replayed);
        return error;
    }

    [[nodiscard]] const State& Current() const noexcept { return state_; }
    [[nodiscard]] std::size_t Pending() const noexcept { return pending_.size(); }
    [[nodiscard]] const Stats& GetStats() const noexcept { return stats_; }

private:
    StepFunction step_;
    DistanceFunction distance_;
    State state_;
    float stepSeconds_;
    double tolerance_;
    std::size_t maxPending_;
    std::uint32_t nextSequence_ = 1U;
    std::deque<std::pair<std::uint32_t, Input>> pending_;
    Stats stats_;
};

// Shows a remote entity slightly in the past, between two server snapshots, so that it moves smoothly
// although snapshots arrive at the tick rate and unevenly. The history is bounded.
template <typename State>
class SnapshotInterpolator final {
public:
    using LerpFunction = std::function<State(const State&, const State&, double)>;

    SnapshotInterpolator(LerpFunction lerp, double delaySeconds, std::size_t maxSnapshots = 32U)
        : lerp_(std::move(lerp)), delay_(delaySeconds), maxSnapshots_(maxSnapshots) {}

    // Snapshots must arrive with growing times; an older one than the newest is dropped (false).
    bool Add(double timeSeconds, const State& state) {
        if (!snapshots_.empty() && timeSeconds <= snapshots_.back().first) {
            return false;
        }
        snapshots_.emplace_back(timeSeconds, state);
        while (snapshots_.size() > maxSnapshots_) {
            snapshots_.pop_front();
        }
        return true;
    }

    // The state to show at `nowSeconds` of the server's clock: delayed by the configured amount, interpolated
    // between the two snapshots around that time, held at the newest one if the time is past it (no
    // extrapolation), nothing before the first snapshot.
    [[nodiscard]] std::optional<State> Sample(double nowSeconds) const {
        if (snapshots_.empty()) {
            return std::nullopt;
        }
        const double target = nowSeconds - delay_;
        if (target <= snapshots_.front().first) {
            return snapshots_.front().second;
        }
        for (std::size_t index = 1U; index < snapshots_.size(); ++index) {
            if (target <= snapshots_[index].first) {
                const double from = snapshots_[index - 1U].first;
                const double span = snapshots_[index].first - from;
                return lerp_(snapshots_[index - 1U].second, snapshots_[index].second, span > 0.0 ? (target - from) / span : 1.0);
            }
        }
        return snapshots_.back().second;
    }

    [[nodiscard]] std::size_t Size() const noexcept { return snapshots_.size(); }

private:
    LerpFunction lerp_;
    double delay_;
    std::size_t maxSnapshots_;
    std::deque<std::pair<double, State>> snapshots_;
};

} // namespace kb::net
