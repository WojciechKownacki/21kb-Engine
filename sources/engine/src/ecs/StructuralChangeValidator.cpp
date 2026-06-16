#include "engine/ecs/StructuralChangeValidator.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace kb::ecs {

StructuralChangeValidator::Guard::Guard(const StructuralChangeValidator& validator) noexcept
    : validator_(&validator) {
    validator_->activeIterations_.fetch_add(1U, std::memory_order_acq_rel);
}

StructuralChangeValidator::Guard::~Guard() {
    Release();
}

StructuralChangeValidator::Guard::Guard(Guard&& other) noexcept
    : validator_(std::exchange(other.validator_, nullptr)) {}

StructuralChangeValidator::Guard& StructuralChangeValidator::Guard::operator=(Guard&& other) noexcept {
    if (this != &other) {
        Release();
        validator_ = std::exchange(other.validator_, nullptr);
    }
    return *this;
}

void StructuralChangeValidator::Guard::Release() noexcept {
    if (validator_ != nullptr) {
        validator_->LeaveIteration();
        validator_ = nullptr;
    }
}

bool StructuralChangeValidator::Guard::Active() const noexcept {
    return validator_ != nullptr;
}

StructuralChangeValidator::Guard StructuralChangeValidator::EnterIteration() const noexcept {
    return Guard{ *this };
}

void StructuralChangeValidator::ValidateStructuralChange(std::string_view operation) const {
    if (activeIterations_.load(std::memory_order_acquire) == 0) {
        return;
    }

    throw std::logic_error(
        "ECS structural change during iteration: " + std::string{ operation } + " must be deferred through CommandBuffer");
}

std::size_t StructuralChangeValidator::ActiveIterationCount() const noexcept {
    return activeIterations_.load(std::memory_order_acquire);
}

void StructuralChangeValidator::LeaveIteration() const noexcept {
    activeIterations_.fetch_sub(1U, std::memory_order_acq_rel);
}

} // namespace kb::ecs
