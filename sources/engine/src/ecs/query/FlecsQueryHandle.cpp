#include "ecs/query/FlecsQueryHandle.hpp"

#include <flecs.h>

#include <utility>

namespace kb::ecs {

FlecsQueryHandle::FlecsQueryHandle(ecs_query_t* query) noexcept
    : query_(query) {}

FlecsQueryHandle::~FlecsQueryHandle() {
    Reset();
}

FlecsQueryHandle::FlecsQueryHandle(FlecsQueryHandle&& other) noexcept
    : query_(std::exchange(other.query_, nullptr)) {}

FlecsQueryHandle& FlecsQueryHandle::operator=(FlecsQueryHandle&& other) noexcept {
    if (this != &other) {
        Reset(std::exchange(other.query_, nullptr));
    }
    return *this;
}

ecs_query_t* FlecsQueryHandle::Get() const noexcept {
    return query_;
}

FlecsQueryHandle::operator bool() const noexcept {
    return query_ != nullptr;
}

void FlecsQueryHandle::Reset(ecs_query_t* query) noexcept {
    if (query_ != nullptr) {
        ecs_query_fini(query_);
    }
    query_ = query;
}

} // namespace kb::ecs
