#include "engine/script/ScriptLifecycle.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace kb::script {

const char* ToString(ScriptLifecycleEvent event) noexcept {
    switch (event) {
    case ScriptLifecycleEvent::Created:
        return "Created";
    case ScriptLifecycleEvent::Activated:
        return "Activated";
    case ScriptLifecycleEvent::Ready:
        return "Ready";
    case ScriptLifecycleEvent::FixedTick:
        return "FixedTick";
    case ScriptLifecycleEvent::Tick:
        return "Tick";
    case ScriptLifecycleEvent::LateTick:
        return "LateTick";
    case ScriptLifecycleEvent::BeforeRender:
        return "BeforeRender";
    case ScriptLifecycleEvent::AfterRender:
        return "AfterRender";
    case ScriptLifecycleEvent::Deactivated:
        return "Deactivated";
    case ScriptLifecycleEvent::Destroyed:
        return "Destroyed";
    }
    return "Tick";
}

kb::visual::VisualGraphLifecycleEvent ToVisualGraphLifecycleEvent(ScriptLifecycleEvent event) noexcept {
    switch (event) {
    case ScriptLifecycleEvent::Created:
        return kb::visual::VisualGraphLifecycleEvent::Created;
    case ScriptLifecycleEvent::Activated:
        return kb::visual::VisualGraphLifecycleEvent::Activated;
    case ScriptLifecycleEvent::Ready:
        return kb::visual::VisualGraphLifecycleEvent::Ready;
    case ScriptLifecycleEvent::FixedTick:
        return kb::visual::VisualGraphLifecycleEvent::FixedTick;
    case ScriptLifecycleEvent::Tick:
        return kb::visual::VisualGraphLifecycleEvent::Tick;
    case ScriptLifecycleEvent::LateTick:
        return kb::visual::VisualGraphLifecycleEvent::LateTick;
    case ScriptLifecycleEvent::BeforeRender:
        return kb::visual::VisualGraphLifecycleEvent::BeforeRender;
    case ScriptLifecycleEvent::AfterRender:
        return kb::visual::VisualGraphLifecycleEvent::AfterRender;
    case ScriptLifecycleEvent::Deactivated:
        return kb::visual::VisualGraphLifecycleEvent::Deactivated;
    case ScriptLifecycleEvent::Destroyed:
        return kb::visual::VisualGraphLifecycleEvent::Destroyed;
    }
    return kb::visual::VisualGraphLifecycleEvent::Tick;
}

std::vector<ForeignLifecycleName> FindForeignLifecycleNames(std::string_view luaSource) {
    static constexpr std::array<std::pair<std::string_view, ScriptLifecycleEvent>, 20> kForeign{ {
        { "Start", ScriptLifecycleEvent::Ready }, { "OnStart", ScriptLifecycleEvent::Ready },
        { "Awake", ScriptLifecycleEvent::Ready }, { "_ready", ScriptLifecycleEvent::Ready },
        { "BeginPlay", ScriptLifecycleEvent::Ready }, { "Update", ScriptLifecycleEvent::Tick },
        { "OnUpdate", ScriptLifecycleEvent::Tick }, { "_process", ScriptLifecycleEvent::Tick },
        { "FixedUpdate", ScriptLifecycleEvent::FixedTick }, { "OnFixedUpdate", ScriptLifecycleEvent::FixedTick },
        { "_physics_process", ScriptLifecycleEvent::FixedTick }, { "LateUpdate", ScriptLifecycleEvent::LateTick },
        { "OnLateUpdate", ScriptLifecycleEvent::LateTick }, { "OnDestroy", ScriptLifecycleEvent::Destroyed },
        { "_exit_tree", ScriptLifecycleEvent::Destroyed }, { "EndPlay", ScriptLifecycleEvent::Destroyed },
        { "OnEnable", ScriptLifecycleEvent::Activated }, { "_enter_tree", ScriptLifecycleEvent::Activated },
        { "OnDisable", ScriptLifecycleEvent::Deactivated }, { "Init", ScriptLifecycleEvent::Created },
    } };
    std::vector<ForeignLifecycleName> found;
    std::uint32_t lineNumber = 0U;
    for (std::size_t start = 0U; start <= luaSource.size();) {
        const std::size_t end = std::min(luaSource.find('\n', start), luaSource.size());
        std::string_view line = luaSource.substr(start, end - start);
        start = end + 1U;
        ++lineNumber;
        // "function Name(" at the start of a line: a global function, not a method or a local.
        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos || !line.substr(first).starts_with("function")) {
            continue;
        }
        line = line.substr(first + 8U);
        const std::size_t nameStart = line.find_first_not_of(" \t");
        if (nameStart == 0U || nameStart == std::string_view::npos) {
            continue;
        }
        std::size_t nameEnd = nameStart;
        while (nameEnd < line.size() &&
               ((line[nameEnd] >= 'A' && line[nameEnd] <= 'Z') || (line[nameEnd] >= 'a' && line[nameEnd] <= 'z') ||
                (line[nameEnd] >= '0' && line[nameEnd] <= '9') || line[nameEnd] == '_')) {
            ++nameEnd;
        }
        const std::string_view name = line.substr(nameStart, nameEnd - nameStart);
        const std::size_t open = line.find_first_not_of(" \t", nameEnd);
        if (open == std::string_view::npos || line[open] != '(') {
            continue;
        }
        for (const auto& [foreign, hook] : kForeign) {
            if (name == foreign) {
                found.push_back(ForeignLifecycleName{ .line = lineNumber, .name = std::string{ name }, .hook = hook });
                break;
            }
        }
    }
    return found;
}

} // namespace kb::script
