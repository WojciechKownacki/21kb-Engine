#pragma once

// JobGraph: an explicit dependency DAG for scheduling parallel work. It models
// jobs, their dependencies, and completion, exposing the pieces the roadmap
// parallel executor needs: ready queues (the frontier of runnable jobs),
// dependency/completion counters, batched levels for wave scheduling, and a
// deterministic topological order with cycle detection.
//
// JobGraph is execution-agnostic: it decides *what is runnable*, while a
// WorkerPool decides *who runs it* (work stealing lives there). Build the graph
// once, Reset() per frame, then drive it with InitialReadyJobs()/OnCompleted().

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace kb::ecs {

class JobGraph {
public:
    using JobIndex = std::size_t;

    [[nodiscard]] JobIndex AddJob() {
        const JobIndex index = nodes_.size();
        nodes_.push_back(Node{});
        return index;
    }

    // Declares that `before` must complete before `after` may start.
    void AddDependency(JobIndex before, JobIndex after) {
        ValidateIndex(before);
        ValidateIndex(after);
        if (before == after) {
            throw std::invalid_argument("JobGraph job cannot depend on itself");
        }
        nodes_[after].dependencyCount += 1U;
        nodes_[before].dependents.push_back(after);
    }

    [[nodiscard]] std::size_t JobCount() const noexcept {
        return nodes_.size();
    }

    // Resets runtime completion state so the graph can be replayed.
    void Reset() {
        completedCount_ = 0U;
        for (Node& node : nodes_) {
            node.remainingDependencies = node.dependencyCount;
            node.completed = false;
        }
    }

    [[nodiscard]] std::vector<JobIndex> InitialReadyJobs() {
        Reset();
        std::vector<JobIndex> ready;
        for (JobIndex index = 0; index < nodes_.size(); ++index) {
            if (nodes_[index].remainingDependencies == 0U) {
                ready.push_back(index);
            }
        }
        return ready;
    }

    // Marks `job` complete and returns jobs whose last dependency just cleared.
    [[nodiscard]] std::vector<JobIndex> OnCompleted(JobIndex job) {
        ValidateIndex(job);
        if (nodes_[job].completed) {
            throw std::logic_error("JobGraph job completed twice");
        }
        if (nodes_[job].remainingDependencies != 0U) {
            throw std::logic_error("JobGraph job completed before its dependencies");
        }
        nodes_[job].completed = true;
        ++completedCount_;

        std::vector<JobIndex> newlyReady;
        for (JobIndex dependent : nodes_[job].dependents) {
            Node& node = nodes_[dependent];
            if (node.remainingDependencies == 0U) {
                continue;
            }
            node.remainingDependencies -= 1U;
            if (node.remainingDependencies == 0U) {
                newlyReady.push_back(dependent);
            }
        }
        return newlyReady;
    }

    [[nodiscard]] std::size_t CompletedCount() const noexcept {
        return completedCount_;
    }

    [[nodiscard]] bool IsComplete() const noexcept {
        return completedCount_ == nodes_.size();
    }

    // Wave levels: each inner vector is a set of jobs runnable in parallel once
    // every earlier wave has finished. Throws on a dependency cycle.
    [[nodiscard]] std::vector<std::vector<JobIndex>> ReadyBatches() const {
        std::vector<std::size_t> remaining(nodes_.size());
        for (JobIndex index = 0; index < nodes_.size(); ++index) {
            remaining[index] = nodes_[index].dependencyCount;
        }

        std::vector<std::vector<JobIndex>> waves;
        std::size_t scheduled = 0U;
        std::vector<JobIndex> current;
        for (JobIndex index = 0; index < nodes_.size(); ++index) {
            if (remaining[index] == 0U) {
                current.push_back(index);
            }
        }

        while (!current.empty()) {
            std::vector<JobIndex> next;
            for (JobIndex job : current) {
                ++scheduled;
                for (JobIndex dependent : nodes_[job].dependents) {
                    if (remaining[dependent] == 0U) {
                        continue;
                    }
                    remaining[dependent] -= 1U;
                    if (remaining[dependent] == 0U) {
                        next.push_back(dependent);
                    }
                }
            }
            waves.push_back(std::move(current));
            current = std::move(next);
        }

        if (scheduled != nodes_.size()) {
            throw std::logic_error("JobGraph contains a dependency cycle");
        }
        return waves;
    }

    // Flattened deterministic execution order respecting dependencies.
    [[nodiscard]] std::vector<JobIndex> TopologicalOrder() const {
        std::vector<JobIndex> order;
        order.reserve(nodes_.size());
        for (const std::vector<JobIndex>& wave : ReadyBatches()) {
            for (JobIndex job : wave) {
                order.push_back(job);
            }
        }
        return order;
    }

    [[nodiscard]] bool HasCycle() const {
        std::vector<std::size_t> remaining(nodes_.size());
        for (JobIndex index = 0; index < nodes_.size(); ++index) {
            remaining[index] = nodes_[index].dependencyCount;
        }
        std::vector<JobIndex> frontier;
        for (JobIndex index = 0; index < nodes_.size(); ++index) {
            if (remaining[index] == 0U) {
                frontier.push_back(index);
            }
        }
        std::size_t scheduled = 0U;
        while (!frontier.empty()) {
            const JobIndex job = frontier.back();
            frontier.pop_back();
            ++scheduled;
            for (JobIndex dependent : nodes_[job].dependents) {
                if (remaining[dependent] == 0U) {
                    continue;
                }
                remaining[dependent] -= 1U;
                if (remaining[dependent] == 0U) {
                    frontier.push_back(dependent);
                }
            }
        }
        return scheduled != nodes_.size();
    }

private:
    struct Node {
        std::vector<JobIndex> dependents;
        std::size_t dependencyCount = 0U;
        std::size_t remainingDependencies = 0U;
        bool completed = false;
    };

    void ValidateIndex(JobIndex index) const {
        if (index >= nodes_.size()) {
            throw std::out_of_range("JobGraph job index is out of range");
        }
    }

    std::vector<Node> nodes_;
    std::size_t completedCount_ = 0U;
};

} // namespace kb::ecs
