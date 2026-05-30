#pragma once

struct ecs_query_t;

namespace kb::ecs {

class FlecsQueryHandle {
public:
    FlecsQueryHandle() noexcept = default;
    explicit FlecsQueryHandle(ecs_query_t* query) noexcept;
    ~FlecsQueryHandle();

    FlecsQueryHandle(const FlecsQueryHandle&) = delete;
    FlecsQueryHandle& operator=(const FlecsQueryHandle&) = delete;

    FlecsQueryHandle(FlecsQueryHandle&& other) noexcept;
    FlecsQueryHandle& operator=(FlecsQueryHandle&& other) noexcept;

    [[nodiscard]] ecs_query_t* Get() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;
    void Reset(ecs_query_t* query = nullptr) noexcept;

private:
    ecs_query_t* query_ = nullptr;
};

} // namespace kb::ecs
