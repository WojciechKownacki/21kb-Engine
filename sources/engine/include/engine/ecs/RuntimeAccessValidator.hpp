#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/SystemAccess.hpp"

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace kb::ecs {

class RuntimeAccessValidator {
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
        Guard(RuntimeAccessValidator& validator, std::size_t token) noexcept;

        RuntimeAccessValidator* validator_ = nullptr;
        std::size_t token_ = 0;

        friend class RuntimeAccessValidator;
    };

    [[nodiscard]] Guard Acquire(std::string_view jobName, const SystemAccess& access, std::size_t workerIndex);
    void Clear() noexcept;
    [[nodiscard]] std::size_t ActiveAccessCount() const noexcept;

private:
    enum class AccessKind {
        Read,
        Write,
    };

    struct ActiveAccess {
        std::size_t token = 0;
        std::size_t workerIndex = 0;
        std::string jobName;
        std::vector<ComponentId> readComponents;
        std::vector<ComponentId> writeComponents;
    };

    void Release(std::size_t token) noexcept;
    [[nodiscard]] static bool HasComponent(const std::vector<ComponentId>& components, ComponentId componentId) noexcept;
    static void ValidateNoConflict(const ActiveAccess& active, const ActiveAccess& pending);
    [[nodiscard]] static std::string BuildConflictMessage(
        const ActiveAccess& active,
        const ActiveAccess& pending,
        ComponentId componentId,
        AccessKind activeKind,
        AccessKind pendingKind);

    mutable std::mutex mutex_;
    std::vector<ActiveAccess> activeAccesses_;
    std::size_t nextToken_ = 1;
};

} // namespace kb::ecs
