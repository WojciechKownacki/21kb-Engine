#pragma once

#include "engine/ecs/ComponentId.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kb::ecs {

class World;

enum class SystemSyncPoint {
    None,
    StructuralChanges,
    AssetBoundary,
    EndOfPhase,
};

class SystemAccess {
public:
    [[nodiscard]] const std::vector<ComponentId>& ReadComponents() const noexcept;
    [[nodiscard]] const std::vector<ComponentId>& WriteComponents() const noexcept;
    [[nodiscard]] const std::vector<std::string>& RunAfter() const noexcept;
    [[nodiscard]] const std::vector<std::string>& RunBefore() const noexcept;
    [[nodiscard]] SystemSyncPoint SyncPoint() const noexcept;
    [[nodiscard]] bool HasSyncPoint() const noexcept;

    SystemAccess& Read(ComponentId componentId);
    SystemAccess& Write(ComponentId componentId);
    SystemAccess& After(std::string_view systemName);
    SystemAccess& Before(std::string_view systemName);
    SystemAccess& RequireSyncPoint(SystemSyncPoint syncPoint);

    template <typename T>
    SystemAccess& Read(World& world);

    template <typename T>
    SystemAccess& Write(World& world);

private:
    static void ValidateComponent(ComponentId componentId);
    static void ValidateSystemName(std::string_view systemName);
    static void ValidateSyncPoint(SystemSyncPoint syncPoint);
    static void AddUnique(std::vector<ComponentId>& components, ComponentId componentId);
    static void AddUnique(std::vector<std::string>& names, std::string_view name);

    std::vector<ComponentId> readComponents_;
    std::vector<ComponentId> writeComponents_;
    std::vector<std::string> runAfter_;
    std::vector<std::string> runBefore_;
    SystemSyncPoint syncPoint_ = SystemSyncPoint::None;
};

} // namespace kb::ecs

#include "engine/ecs/SystemAccess.inl"
