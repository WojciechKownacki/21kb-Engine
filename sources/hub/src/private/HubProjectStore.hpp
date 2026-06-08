#pragma once

#include "HubState.hpp"

#include <filesystem>
#include <vector>

namespace kb::hub {

class HubProjectStore {
public:
    HubProjectStore() = delete;

    [[nodiscard]] static std::vector<HubProjectItem> Load();
    static void Save(const std::vector<HubProjectItem>& projects);
    static void AddOrPromote(std::vector<HubProjectItem>& projects, const std::filesystem::path& projectFile);

private:
    [[nodiscard]] static std::filesystem::path StoreFile();
    [[nodiscard]] static HubProjectItem BuildProjectItem(const std::filesystem::path& projectFile);
};

} // namespace kb::hub
