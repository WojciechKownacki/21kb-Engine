#include "engine/ecs/RuntimeAccessValidator.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace kb::ecs {

RuntimeAccessValidator::Guard::Guard(RuntimeAccessValidator& validator, std::size_t token) noexcept
    : validator_(&validator)
    , token_(token) {}

RuntimeAccessValidator::Guard::~Guard() {
    Release();
}

RuntimeAccessValidator::Guard::Guard(Guard&& other) noexcept
    : validator_(std::exchange(other.validator_, nullptr))
    , token_(std::exchange(other.token_, 0)) {}

RuntimeAccessValidator::Guard& RuntimeAccessValidator::Guard::operator=(Guard&& other) noexcept {
    if (this != &other) {
        Release();
        validator_ = std::exchange(other.validator_, nullptr);
        token_ = std::exchange(other.token_, 0);
    }
    return *this;
}

void RuntimeAccessValidator::Guard::Release() noexcept {
    if (validator_ != nullptr) {
        validator_->Release(token_);
        validator_ = nullptr;
        token_ = 0;
    }
}

bool RuntimeAccessValidator::Guard::Active() const noexcept {
    return validator_ != nullptr;
}

RuntimeAccessValidator::Guard RuntimeAccessValidator::Acquire(std::string_view jobName, const SystemAccess& access, std::size_t workerIndex) {
    ActiveAccess pending{
        .token = 0,
        .workerIndex = workerIndex,
        .jobName = std::string{ jobName },
        .readComponents = access.ReadComponents(),
        .writeComponents = access.WriteComponents(),
    };

    std::lock_guard lock{ mutex_ };
    for (const ActiveAccess& active : activeAccesses_) {
        ValidateNoConflict(active, pending);
    }

    pending.token = nextToken_++;
    activeAccesses_.push_back(std::move(pending));
    return Guard{ *this, activeAccesses_.back().token };
}

void RuntimeAccessValidator::Clear() noexcept {
    std::lock_guard lock{ mutex_ };
    activeAccesses_.clear();
}

std::size_t RuntimeAccessValidator::ActiveAccessCount() const noexcept {
    std::lock_guard lock{ mutex_ };
    return activeAccesses_.size();
}

void RuntimeAccessValidator::Release(std::size_t token) noexcept {
    std::lock_guard lock{ mutex_ };
    const auto found = std::find_if(activeAccesses_.begin(), activeAccesses_.end(), [token](const ActiveAccess& active) {
        return active.token == token;
    });
    if (found != activeAccesses_.end()) {
        activeAccesses_.erase(found);
    }
}

bool RuntimeAccessValidator::HasComponent(const std::vector<ComponentId>& components, ComponentId componentId) noexcept {
    return std::binary_search(components.begin(), components.end(), componentId);
}

void RuntimeAccessValidator::ValidateNoConflict(const ActiveAccess& active, const ActiveAccess& pending) {
    for (ComponentId componentId : pending.writeComponents) {
        if (HasComponent(active.writeComponents, componentId)) {
            throw std::logic_error(BuildConflictMessage(active, pending, componentId, AccessKind::Write, AccessKind::Write));
        }
        if (HasComponent(active.readComponents, componentId)) {
            throw std::logic_error(BuildConflictMessage(active, pending, componentId, AccessKind::Read, AccessKind::Write));
        }
    }

    for (ComponentId componentId : pending.readComponents) {
        if (HasComponent(active.writeComponents, componentId)) {
            throw std::logic_error(BuildConflictMessage(active, pending, componentId, AccessKind::Write, AccessKind::Read));
        }
    }
}

std::string RuntimeAccessValidator::BuildConflictMessage(
    const ActiveAccess& active,
    const ActiveAccess& pending,
    ComponentId componentId,
    AccessKind activeKind,
    AccessKind pendingKind) {
    const auto kindName = [](AccessKind kind) noexcept -> const char* {
        return kind == AccessKind::Write ? "write" : "read";
    };

    return "ECS runtime access conflict: job '" + pending.jobName + "' " + kindName(pendingKind) +
        " conflicts with active job '" + active.jobName + "' " + kindName(activeKind) +
        " on component " + std::to_string(componentId) +
        " (workers " + std::to_string(pending.workerIndex) + " and " + std::to_string(active.workerIndex) + ")";
}

} // namespace kb::ecs
