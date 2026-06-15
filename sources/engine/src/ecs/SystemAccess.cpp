#include "engine/ecs/SystemAccess.hpp"

namespace kb::ecs {

const std::vector<ComponentId>& SystemAccess::ReadComponents() const noexcept {
    return readComponents_;
}

const std::vector<ComponentId>& SystemAccess::WriteComponents() const noexcept {
    return writeComponents_;
}

const std::vector<std::string>& SystemAccess::RunAfter() const noexcept {
    return runAfter_;
}

const std::vector<std::string>& SystemAccess::RunBefore() const noexcept {
    return runBefore_;
}

SystemSyncPoint SystemAccess::SyncPoint() const noexcept {
    return syncPoint_;
}

bool SystemAccess::HasSyncPoint() const noexcept {
    return syncPoint_ != SystemSyncPoint::None;
}

SystemAccess& SystemAccess::Read(ComponentId componentId) {
    ValidateComponent(componentId);
    if (!std::binary_search(writeComponents_.begin(), writeComponents_.end(), componentId)) {
        AddUnique(readComponents_, componentId);
    }
    return *this;
}

SystemAccess& SystemAccess::Write(ComponentId componentId) {
    ValidateComponent(componentId);
    readComponents_.erase(std::remove(readComponents_.begin(), readComponents_.end(), componentId), readComponents_.end());
    AddUnique(writeComponents_, componentId);
    return *this;
}

SystemAccess& SystemAccess::After(std::string_view systemName) {
    ValidateSystemName(systemName);
    AddUnique(runAfter_, systemName);
    return *this;
}

SystemAccess& SystemAccess::Before(std::string_view systemName) {
    ValidateSystemName(systemName);
    AddUnique(runBefore_, systemName);
    return *this;
}

SystemAccess& SystemAccess::RequireSyncPoint(SystemSyncPoint syncPoint) {
    ValidateSyncPoint(syncPoint);
    syncPoint_ = syncPoint;
    return *this;
}

void SystemAccess::ValidateComponent(ComponentId componentId) {
    if (componentId == 0) {
        throw std::invalid_argument("ECS system access cannot reference an invalid component");
    }
}

void SystemAccess::ValidateSystemName(std::string_view systemName) {
    if (systemName.empty()) {
        throw std::invalid_argument("ECS system ordering cannot reference an empty system name");
    }
}

void SystemAccess::ValidateSyncPoint(SystemSyncPoint syncPoint) {
    if (syncPoint == SystemSyncPoint::None) {
        throw std::invalid_argument("ECS system sync point requires a runtime boundary reason");
    }
}

void SystemAccess::AddUnique(std::vector<ComponentId>& components, ComponentId componentId) {
    const auto insertPosition = std::lower_bound(components.begin(), components.end(), componentId);
    if (insertPosition == components.end() || *insertPosition != componentId) {
        components.insert(insertPosition, componentId);
    }
}

void SystemAccess::AddUnique(std::vector<std::string>& names, std::string_view name) {
    const auto existing = std::find(names.begin(), names.end(), name);
    if (existing == names.end()) {
        names.emplace_back(name);
    }
}

} // namespace kb::ecs
