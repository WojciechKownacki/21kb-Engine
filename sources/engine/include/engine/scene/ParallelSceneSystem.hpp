#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

// What an update of a scene system reads and writes, as resource names the systems agree on.
// A name is any string; two systems conflict when one writes a name the other reads or writes.
struct SceneSystemAccess {
    std::vector<std::string> reads;
    std::vector<std::string> writes;
};

// Resource names for the parts of a scene that every system may touch. A system that creates or
// destroys entities, adds or removes components, or reparents must declare kSceneStructure as a
// write; a system that reads those structures declares it as a read.
inline constexpr std::string_view kSceneStructureResource = "scene:structure";
inline constexpr std::string_view kSceneTransformsResource = "scene:transforms";

// A scene system implements this, in addition to SceneSystem, to allow its OnUpdate and
// OnFixedUpdate to run at the same time as the update of other systems that do not conflict with it.
//
// The scheduler still runs systems in registration order. It runs a group of consecutive systems
// together only when every one of them implements this interface and no two of them conflict, so
// the result equals the serial result as long as the declared access is true. A system that does
// not implement the interface is a barrier: everything registered before it finishes first, and
// everything after it starts after it.
//
// The promise a system makes by implementing it:
//   * its updates touch only the resources it declared, and state private to the system;
//   * its updates are safe to run on a worker thread (no main-thread-only API, no thread-local
//     state carried between calls);
//   * DeclareAccess does not change after the system has been added.
// Errors a system reports or throws are recorded in registration order after the group finishes.
class IParallelSceneSystem {
public:
    IParallelSceneSystem() = default;
    IParallelSceneSystem(const IParallelSceneSystem&) = delete;
    IParallelSceneSystem& operator=(const IParallelSceneSystem&) = delete;
    virtual ~IParallelSceneSystem() = default;

    [[nodiscard]] virtual SceneSystemAccess DeclareAccess() const = 0;
};

} // namespace kb::scene
