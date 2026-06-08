#pragma once

#include <string_view>

namespace kb::project {

struct ProjectDescriptorFormat {
    static constexpr std::string_view Header = "21kb.project.v1";
    static constexpr std::string_view Extension = ".21kbproject";

    static constexpr std::string_view FileVersionKey = "fileVersion";
    static constexpr std::string_view EngineAssociationKey = "engineAssociation";
    static constexpr std::string_view NameKey = "name";
    static constexpr std::string_view CategoryKey = "category";
    static constexpr std::string_view DescriptionKey = "description";
    static constexpr std::string_view ContentRootKey = "contentRoot";
    static constexpr std::string_view DefaultSceneKey = "defaultScene";
    static constexpr std::string_view DisableEnginePluginsByDefaultKey = "disableEnginePluginsByDefault";
    static constexpr std::string_view TargetPlatformsCountKey = "targetPlatforms.count";
    static constexpr std::string_view ModulesCountKey = "modules.count";
    static constexpr std::string_view PluginsCountKey = "plugins.count";
};

} // namespace kb::project
