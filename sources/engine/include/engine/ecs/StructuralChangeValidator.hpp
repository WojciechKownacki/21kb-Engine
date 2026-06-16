#pragma once

#include <atomic>
#include <cstddef>
#include <string_view>

namespace kb::ecs {

class StructuralChangeValidator {
public:
    class Guard {
    public:
        Guard() noexcept = default;
        ~Guard();

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        Guard(Guard&& other) noexcept;
        Guard& operator=(Guard&& other) noexcept;

        void Release() noexcept;
        [[nodiscard]] bool Active() const noexcept;

    private:
        explicit Guard(const StructuralChangeValidator& validator) noexcept;

        const StructuralChangeValidator* validator_ = nullptr;

        friend class StructuralChangeValidator;
    };

    [[nodiscard]] Guard EnterIteration() const noexcept;
    void ValidateStructuralChange(std::string_view operation) const;
    [[nodiscard]] std::size_t ActiveIterationCount() const noexcept;

private:
    void LeaveIteration() const noexcept;

    mutable std::atomic<std::size_t> activeIterations_ = 0;
};

} // namespace kb::ecs
